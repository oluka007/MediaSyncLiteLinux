/* tags.h ---
 *
 * Filename: tags.h
 * Description: Lightweight audio metadata (ID3v2/ID3v1/Vorbis comment)
 *              reader used to power nicer file listing and stronger
 *              duplicate-upload detection (tag-based, in addition to
 *              plain MD5 matching).
 *
 */

#ifndef __TAGS_H_
#define __TAGS_H_

typedef struct __audio_tags_ {
    char *title;
    char *artist;
    char *album;
    char *genre;
    int   track;
    int   year;
} audio_tags_t;

/* Attempts to read common metadata tags (ID3v2, ID3v1, Vorbis comments in
 * FLAC/Ogg/Opus) from the file at `path`. Returns NULL if the file could
 * not be opened or no recognisable tag data was found. Caller must free
 * the result with free_audio_tags(). */
audio_tags_t *read_audio_tags(const char *path);

/* Frees an audio_tags_t returned by read_audio_tags(). Safe to call with NULL. */
void free_audio_tags(audio_tags_t *tags);

/* Builds a normalized "signature" string (lower-cased, whitespace
 * collapsed) from title/artist/album/track, suitable for use as a
 * duplicate-detection key. Requires at least title+artist to be present;
 * returns NULL otherwise (caller should fall back to MD5-only checks). */
char *build_tag_signature(const audio_tags_t *tags);

/* Builds a human friendly "Artist - Title" label for display purposes,
 * falling back to the file's basename when tags are unavailable. Never
 * returns NULL (unless memory allocation fails). */
char *build_display_name(const audio_tags_t *tags, const char *path);

#endif
/* tags.h ends here */
