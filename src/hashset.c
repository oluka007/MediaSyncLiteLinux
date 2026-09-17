/* hashset.c ---
 *
 * Filename: hashset.c
 * Description: Small, dependency-free case-insensitive string hash set
 *              implementation. See hashset.h for rationale.
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "hashset.h"

/* djb2 hash, case-insensitive */
static size_t hs_hash(const char *key) {

    size_t hash = 5381;
    int c;

    while((c = (unsigned char)*key++) != 0)
        hash = ((hash << 5) + hash) + (size_t)tolower(c);

    return hash;
}

hash_set_t *hash_set_new(size_t initial_buckets) {

    hash_set_t *hs;

    if(initial_buckets == 0)
        initial_buckets = 256;

    if((hs = (hash_set_t *)malloc(sizeof(hash_set_t))) == NULL)
        return NULL;

    if((hs->buckets = (hs_node_t **)calloc(initial_buckets, sizeof(hs_node_t *))) == NULL) {
        free(hs);
        return NULL;
    }

    hs->nbuckets = initial_buckets;
    hs->count = 0;

    return hs;
}

int hash_set_contains(const hash_set_t *hs, const char *key) {

    size_t idx;
    hs_node_t *node;

    if(hs == NULL || key == NULL)
        return 0;

    idx = hs_hash(key) % hs->nbuckets;

    for(node = hs->buckets[idx]; node != NULL; node = node->next)
        if(strcasecmp(node->key, key) == 0)
            return 1;

    return 0;
}

int hash_set_add(hash_set_t *hs, const char *key) {

    size_t idx;
    hs_node_t *node;

    if(hs == NULL || key == NULL)
        return 0;

    if(hash_set_contains(hs, key))
        return 0;

    if((node = (hs_node_t *)malloc(sizeof(hs_node_t))) == NULL)
        return 0;

    if((node->key = strdup(key)) == NULL) {
        free(node);
        return 0;
    }

    idx = hs_hash(key) % hs->nbuckets;
    node->next = hs->buckets[idx];
    hs->buckets[idx] = node;
    hs->count++;

    return 1;
}

void hash_set_free(hash_set_t *hs) {

    size_t i;
    hs_node_t *node, *next;

    if(hs == NULL)
        return;

    for(i = 0; i < hs->nbuckets; i++) {
        node = hs->buckets[i];
        while(node != NULL) {
            next = node->next;
            free(node->key);
            free(node);
            node = next;
        }
    }

    free(hs->buckets);
    free(hs);
}

/* hashset.c ends here */
