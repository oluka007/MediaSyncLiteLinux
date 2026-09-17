/* tags.c ---
 *
 * Filename: tags.c
 * Description: Lightweight audio metadata reader.
 *
 * Supports:
 *   - ID3v2 (2.2/2.3/2.4) text frames: TIT2/TT2, TPE1/TP1, TALB/TAL,
 *     TRCK/TRK, TCON/TCO, TYER/TYE/TDRC
 *   - ID3v1 (trailing 128 byte "TAG" block), used to fill in anything
 *     ID3v2 didn't provide
 *   - FLAC Vorbis comment metadata blocks
 *   - Ogg Vorbis / Ogg Opus comment headers (best-effort byte scan; see
 *     caveat below)
 *
 * This is intentionally not a full-featured tag library - it only reads
 * what MediaSync Lite needs (title/artist/album/track/genre/year) in
 * order to (a) show nicer information while scanning/uploading and
 * (b) build a "tag signature" that is used, alongside MD5 hashing, to
 * catch duplicate uploads that a pure byte-for-byte MD5 comparison
 * would miss (e.g. the same song re-encoded, or re-tagged copies).
 *
 * Caveat: the Ogg reader works by scanning the first chunk of the file
 * for the Vorbis/Opus comment header magic and parsing forward as if
 * the packet were contiguous. This is true for the vast majority of
 * real-world files (the comment header packet is small and fits in a
 * single Ogg page), but a comment header that happens to span a page
 * boundary could be parsed incorrectly. Failure here always degrades
 * gracefully -- we simply don't get tag info and fall back to
 * MD5-only duplicate detection for that file.
 *
 */

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>

#include "tags.h"

#define __ID3V2_MAX_READ_        (2 * 1024 * 1024)
#define __OGG_SCAN_WINDOW_       (256 * 1024)
#define __VORBIS_FIELD_MAX_      (1024 * 1024)

static char *dup_trimmed(const char *s, size_t len) {

    char *out;
    size_t start = 0, end = len;

    if(s == NULL)
        return NULL;

    while(start < len && (unsigned char)s[start] <= ' ')
        start++;
    while(end > start && (unsigned char)s[end - 1] <= ' ')
        end--;

    if(end <= start)
        return NULL;

    if((out = (char *)malloc(end - start + 1)) == NULL)
        return NULL;

    memcpy(out, s + start, end - start);
    out[end - start] = '\0';

    return out;
}

static char *latin1_to_utf8(const unsigned char *data, size_t len) {

    char *out;
    size_t i, o = 0;

    if((out = (char *)malloc(len * 2 + 1)) == NULL)
        return NULL;

    for(i = 0; i < len; i++) {
        unsigned char b = data[i];
        if(b == 0)
            break;
        if(b < 0x80) {
            out[o++] = (char)b;
        } else {
            out[o++] = (char)(0xC0 | (b >> 6));
            out[o++] = (char)(0x80 | (b & 0x3F));
        }
    }
    out[o] = '\0';

    return out;
}

static char *utf16_to_utf8(const unsigned char *data, size_t len, int big_endian) {

    char *out;
    size_t i = 0, o = 0;
    unsigned int cp;

    if(len >= 2 &&
       ((data[0] == 0xFF && data[1] == 0xFE) ||
        (data[0] == 0xFE && data[1] == 0xFF))) {
        big_endian = (data[0] == 0xFE);
        i = 2;
    }

    if((out = (char *)malloc(len * 3 + 1)) == NULL)
        return NULL;

    for(; i + 1 < len; i += 2) {
        cp = big_endian ? ((unsigned int)data[i] << 8 | data[i+1])
                         : ((unsigned int)data[i+1] << 8 | data[i]);

        if(cp == 0)
            break;

        /* Skip surrogate pairs (outside BMP) - rare in tags, just drop them */
        if(cp >= 0xD800 && cp <= 0xDFFF) {
            i += 2;
            continue;
        }

        if(cp < 0x80) {
            out[o++] = (char)cp;
        } else if(cp < 0x800) {
            out[o++] = (char)(0xC0 | (cp >> 6));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[o++] = (char)(0xE0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[o] = '\0';

    return out;
}

static char *id3_text_to_utf8(const unsigned char *data, size_t len) {

    unsigned char encoding;

    if(data == NULL || len == 0)
        return NULL;

    encoding = data[0];
    data++;
    len--;

    switch(encoding) {
        case 0: /* ISO-8859-1 */
            return latin1_to_utf8(data, len);
        case 1: /* UTF-16 with BOM */
            return utf16_to_utf8(data, len, 0);
        case 2: /* UTF-16BE, no BOM */
            return utf16_to_utf8(data, len, 1);
        case 3: /* UTF-8 */
            return dup_trimmed((const char *)data, len);
        default:
            return latin1_to_utf8(data, len);
    }
}

static int parse_leading_int(const char *s) {

    if(s == NULL)
        return 0;

    while(*s == ' ' || *s == '\t')
        s++;

    return atoi(s);
}

static unsigned int synchsafe_to_uint(const unsigned char *b) {
    return ((unsigned int)(b[0] & 0x7F) << 21) |
           ((unsigned int)(b[1] & 0x7F) << 14) |
           ((unsigned int)(b[2] & 0x7F) << 7)  |
           ((unsigned int)(b[3] & 0x7F));
}

static unsigned int be32_to_uint(const unsigned char *b) {
    return ((unsigned int)b[0] << 24) | ((unsigned int)b[1] << 16) |
           ((unsigned int)b[2] << 8)  | ((unsigned int)b[3]);
}

static void set_field(char **field, char *value) {

    if(value == NULL || *value == '\0') {
        free(value);
        return;
    }

    if(*field != NULL)
        free(*field);

    *field = value;
}

static int read_id3v2(FILE *fp, audio_tags_t *tags) {

    unsigned char header[10];
    unsigned int tag_size, major, pos = 0, read_len;
    unsigned char *buf;
    int found = 0;

    if(fseek(fp, 0, SEEK_SET) != 0)
        return 0;

    if(fread(header, 1, 10, fp) != 10)
        return 0;

    if(memcmp(header, "ID3", 3) != 0)
        return 0;

    major = header[3];
    tag_size = synchsafe_to_uint(header + 6);

    if(tag_size == 0)
        return 0;

    read_len = (tag_size < __ID3V2_MAX_READ_) ? tag_size : __ID3V2_MAX_READ_;

    if((buf = (unsigned char *)malloc(read_len)) == NULL)
        return 0;

    if(fread(buf, 1, read_len, fp) != read_len) {
        free(buf);
        return 0;
    }

    if(major == 2) {
        /* ID3v2.2: 3 byte id, 3 byte size (plain BE), no flags */
        while(pos + 6 <= read_len) {
            char id[4];
            unsigned int fsize;

            memcpy(id, buf + pos, 3);
            id[3] = '\0';

            if(id[0] == '\0')
                break;

            fsize = ((unsigned int)buf[pos+3] << 16) | ((unsigned int)buf[pos+4] << 8) | buf[pos+5];
            pos += 6;

            if(fsize == 0 || pos + fsize > read_len)
                break;

            if(strcmp(id, "TT2") == 0) {
                set_field(&tags->title, id3_text_to_utf8(buf + pos, fsize));
                found = 1;
            } else if(strcmp(id, "TP1") == 0) {
                set_field(&tags->artist, id3_text_to_utf8(buf + pos, fsize));
                found = 1;
            } else if(strcmp(id, "TAL") == 0) {
                set_field(&tags->album, id3_text_to_utf8(buf + pos, fsize));
                found = 1;
            } else if(strcmp(id, "TRK") == 0) {
                char *v = id3_text_to_utf8(buf + pos, fsize);
                if(v != NULL) { tags->track = parse_leading_int(v); free(v); found = 1; }
            } else if(strcmp(id, "TCO") == 0) {
                set_field(&tags->genre, id3_text_to_utf8(buf + pos, fsize));
                found = 1;
            } else if(strcmp(id, "TYE") == 0) {
                char *v = id3_text_to_utf8(buf + pos, fsize);
                if(v != NULL) { tags->year = parse_leading_int(v); free(v); found = 1; }
            }

            pos += fsize;
        }
    } else {
        /* ID3v2.3 / ID3v2.4: 4 byte id, 4 byte size, 2 byte flags */
        while(pos + 10 <= read_len) {
            char id[5];
            unsigned int fsize;

            memcpy(id, buf + pos, 4);
            id[4] = '\0';

            if(id[0] == '\0')
                break;

            if(major >= 4)
                fsize = synchsafe_to_uint(buf + pos + 4);
            else
                fsize = be32_to_uint(buf + pos + 4);

            pos += 10;

            if(fsize == 0 || pos + fsize > read_len)
                break;

            if(strcmp(id, "TIT2") == 0) {
                set_field(&tags->title, id3_text_to_utf8(buf + pos, fsize));
                found = 1;
            } else if(strcmp(id, "TPE1") == 0) {
                set_field(&tags->artist, id3_text_to_utf8(buf + pos, fsize));
                found = 1;
            } else if(strcmp(id, "TALB") == 0) {
                set_field(&tags->album, id3_text_to_utf8(buf + pos, fsize));
                found = 1;
            } else if(strcmp(id, "TRCK") == 0) {
                char *v = id3_text_to_utf8(buf + pos, fsize);
                if(v != NULL) { tags->track = parse_leading_int(v); free(v); found = 1; }
            } else if(strcmp(id, "TCON") == 0) {
                set_field(&tags->genre, id3_text_to_utf8(buf + pos, fsize));
                found = 1;
            } else if(strcmp(id, "TYER") == 0 || strcmp(id, "TDRC") == 0) {
                char *v = id3_text_to_utf8(buf + pos, fsize);
                if(v != NULL) { tags->year = parse_leading_int(v); free(v); found = 1; }
            }

            pos += fsize;
        }
    }

    free(buf);

    return found;
}

static int read_id3v1(FILE *fp, audio_tags_t *tags) {

    unsigned char tag[128];
    long fsize;

    if(fseek(fp, 0, SEEK_END) != 0)
        return 0;

    if((fsize = ftell(fp)) < 128)
        return 0;

    if(fseek(fp, -128, SEEK_END) != 0)
        return 0;

    if(fread(tag, 1, 128, fp) != 128)
        return 0;

    if(memcmp(tag, "TAG", 3) != 0)
        return 0;

    if(tags->title == NULL)
        set_field(&tags->title, dup_trimmed((const char *)tag + 3, 30));
    if(tags->artist == NULL)
        set_field(&tags->artist, dup_trimmed((const char *)tag + 33, 30));
    if(tags->album == NULL)
        set_field(&tags->album, dup_trimmed((const char *)tag + 63, 30));

    if(tags->year == 0) {
        char year[5];
        memcpy(year, tag + 93, 4);
        year[4] = '\0';
        tags->year = parse_leading_int(year);
    }

    /* ID3v1.1: byte 125 is zero and byte 126 holds the track number */
    if(tags->track == 0 && tag[125] == 0 && tag[126] != 0)
        tags->track = tag[126];

    return 1;
}

static void parse_vorbis_comment_block(const unsigned char *data, size_t len, audio_tags_t *tags) {

    size_t pos = 0;
    unsigned int vendor_len, comment_count, i;

    if(len < 8)
        return;

    /* Vorbis comment fields are little-endian. */
    vendor_len = (unsigned int)data[0] | ((unsigned int)data[1] << 8) |
                 ((unsigned int)data[2] << 16) | ((unsigned int)data[3] << 24);
    pos = 4;

    if(vendor_len > __VORBIS_FIELD_MAX_ || pos + vendor_len > len)
        return;

    pos += vendor_len;

    if(pos + 4 > len)
        return;

    comment_count = (unsigned int)data[pos] | ((unsigned int)data[pos+1] << 8) |
                    ((unsigned int)data[pos+2] << 16) | ((unsigned int)data[pos+3] << 24);
    pos += 4;

    for(i = 0; i < comment_count && pos + 4 <= len; i++) {

        unsigned int clen;
        const char *field;
        const char *eq;

        clen = (unsigned int)data[pos] | ((unsigned int)data[pos+1] << 8) |
               ((unsigned int)data[pos+2] << 16) | ((unsigned int)data[pos+3] << 24);
        pos += 4;

        if(clen == 0 || clen > __VORBIS_FIELD_MAX_ || pos + clen > len)
            break;

        field = (const char *)(data + pos);
        eq = memchr(field, '=', clen);

        if(eq != NULL) {
            size_t key_len = (size_t)(eq - field);
            size_t val_len = clen - key_len - 1;
            const char *val = eq + 1;

            if(strncasecmp(field, "TITLE", key_len) == 0 && key_len == 5)
                set_field(&tags->title, dup_trimmed(val, val_len));
            else if(strncasecmp(field, "ARTIST", key_len) == 0 && key_len == 6)
                set_field(&tags->artist, dup_trimmed(val, val_len));
            else if(strncasecmp(field, "ALBUM", key_len) == 0 && key_len == 5)
                set_field(&tags->album, dup_trimmed(val, val_len));
            else if(strncasecmp(field, "GENRE", key_len) == 0 && key_len == 5)
                set_field(&tags->genre, dup_trimmed(val, val_len));
            else if(strncasecmp(field, "TRACKNUMBER", key_len) == 0 && key_len == 11) {
                char *v = dup_trimmed(val, val_len);
                if(v != NULL) { tags->track = parse_leading_int(v); free(v); }
            } else if(strncasecmp(field, "DATE", key_len) == 0 && key_len == 4) {
                char *v = dup_trimmed(val, val_len);
                if(v != NULL) { tags->year = parse_leading_int(v); free(v); }
            }
        }

        pos += clen;
    }
}

static int read_flac_tags(FILE *fp, audio_tags_t *tags) {

    unsigned char magic[4];
    int found = 0;

    if(fseek(fp, 0, SEEK_SET) != 0)
        return 0;

    if(fread(magic, 1, 4, fp) != 4 || memcmp(magic, "fLaC", 4) != 0)
        return 0;

    for(;;) {
        unsigned char bh[4];
        unsigned int block_type, block_len;
        int is_last;

        if(fread(bh, 1, 4, fp) != 4)
            break;

        is_last = (bh[0] & 0x80) != 0;
        block_type = bh[0] & 0x7F;
        block_len = ((unsigned int)bh[1] << 16) | ((unsigned int)bh[2] << 8) | bh[3];

        if(block_type == 4) { /* VORBIS_COMMENT */
            unsigned char *buf;
            unsigned int rlen = (block_len < __VORBIS_FIELD_MAX_) ? block_len : __VORBIS_FIELD_MAX_;

            if((buf = (unsigned char *)malloc(rlen)) != NULL) {
                if(fread(buf, 1, rlen, fp) == rlen) {
                    parse_vorbis_comment_block(buf, rlen, tags);
                    found = 1;
                }
                free(buf);
            }
            if(rlen < block_len)
                fseek(fp, block_len - rlen, SEEK_CUR);
        } else {
            if(fseek(fp, block_len, SEEK_CUR) != 0)
                break;
        }

        if(is_last)
            break;
    }

    return found;
}

static int read_ogg_tags(const char *path, audio_tags_t *tags) {

    FILE *fp;
    unsigned char *buf;
    size_t rd, i, off;
    int found = 0;

    if((fp = fopen(path, "rb")) == NULL)
        return 0;

    if((buf = (unsigned char *)malloc(__OGG_SCAN_WINDOW_)) == NULL) {
        fclose(fp);
        return 0;
    }

    rd = fread(buf, 1, __OGG_SCAN_WINDOW_, fp);
    fclose(fp);

    off = (size_t)-1;

    for(i = 0; i + 7 <= rd; i++) {
        if(buf[i] == 0x03 && memcmp(buf + i + 1, "vorbis", 6) == 0) {
            off = i + 7;
            break;
        }
        if(i + 8 <= rd && memcmp(buf + i, "OpusTags", 8) == 0) {
            off = i + 8;
            break;
        }
    }

    if(off != (size_t)-1 && off < rd) {
        parse_vorbis_comment_block(buf + off, rd - off, tags);
        found = 1;
    }

    free(buf);

    return found;
}

audio_tags_t *read_audio_tags(const char *path) {

    FILE *fp;
    audio_tags_t *tags;
    unsigned char magic[4];
    int any = 0;

    if((fp = fopen(path, "rb")) == NULL)
        return NULL;

    if(fread(magic, 1, 4, fp) != 4) {
        fclose(fp);
        return NULL;
    }

    if((tags = (audio_tags_t *)calloc(1, sizeof(audio_tags_t))) == NULL) {
        fclose(fp);
        return NULL;
    }

    if(memcmp(magic, "fLaC", 4) == 0) {
        any |= read_flac_tags(fp, tags);
        fclose(fp);
    } else if(memcmp(magic, "OggS", 4) == 0) {
        fclose(fp);
        any |= read_ogg_tags(path, tags);
    } else {
        any |= read_id3v2(fp, tags);
        any |= read_id3v1(fp, tags);
        fclose(fp);
    }

    if(!any || (tags->title == NULL && tags->artist == NULL && tags->album == NULL)) {
        free_audio_tags(tags);
        return NULL;
    }

    return tags;
}

void free_audio_tags(audio_tags_t *tags) {

    if(tags == NULL)
        return;

    free(tags->title);
    free(tags->artist);
    free(tags->album);
    free(tags->genre);
    free(tags);
}

static void append_normalized(char **out, size_t *out_len, size_t *out_cap, const char *s) {

    size_t i, len;

    if(s == NULL)
        return;

    len = strlen(s);

    if(*out_len + len + 2 > *out_cap) {
        size_t new_cap = (*out_cap + len + 2) * 2;
        char *tmp = (char *)realloc(*out, new_cap);
        if(tmp == NULL)
            return;
        *out = tmp;
        *out_cap = new_cap;
    }

    for(i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if(isspace(c)) {
            if(*out_len > 0 && (*out)[*out_len - 1] != ' ')
                (*out)[(*out_len)++] = ' ';
        } else {
            (*out)[(*out_len)++] = (char)tolower(c);
        }
    }
    (*out)[(*out_len)++] = '|';
    (*out)[*out_len] = '\0';
}

char *build_tag_signature(const audio_tags_t *tags) {

    char *out;
    size_t out_len = 0, out_cap = 64;
    char track_str[16];

    if(tags == NULL || tags->title == NULL || tags->artist == NULL)
        return NULL;

    if((out = (char *)malloc(out_cap)) == NULL)
        return NULL;
    out[0] = '\0';

    append_normalized(&out, &out_len, &out_cap, tags->title);
    append_normalized(&out, &out_len, &out_cap, tags->artist);

    if(tags->album != NULL)
        append_normalized(&out, &out_len, &out_cap, tags->album);

    if(tags->track > 0) {
        snprintf(track_str, sizeof(track_str), "%d", tags->track);
        append_normalized(&out, &out_len, &out_cap, track_str);
    }

    if(out_len == 0) {
        free(out);
        return NULL;
    }

    return out;
}

char *build_display_name(const audio_tags_t *tags, const char *path) {

    char *out;
    const char *base;

    if(tags != NULL && tags->artist != NULL && tags->title != NULL) {
        size_t len = strlen(tags->artist) + strlen(tags->title) + 4;
        if((out = (char *)malloc(len)) != NULL)
            snprintf(out, len, "%s - %s", tags->artist, tags->title);
        return out;
    }

    if(tags != NULL && tags->title != NULL)
        return strdup(tags->title);

    if((base = strrchr(path, '/')) != NULL)
        base++;
    else
        base = path;

    return strdup(base);
}

/* tags.c ends here */
