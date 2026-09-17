/* hashset.h ---
 *
 * Filename: hashset.h
 * Description: Small, dependency-free case-insensitive string hash set.
 *              Used to make MD5/tag duplicate lookups O(1) instead of
 *              the O(n) linear scans previously used while walking the
 *              directory tree (which made scanning large libraries for
 *              duplicates effectively O(n^2)).
 *
 */

#ifndef __HASHSET_H_
#define __HASHSET_H_

#include <stddef.h>

typedef struct __hs_node_ {
    char *key;
    struct __hs_node_ *next;
} hs_node_t;

typedef struct __hash_set_ {
    hs_node_t **buckets;
    size_t nbuckets;
    size_t count;
} hash_set_t;

/* Creates a new, empty hash set. */
hash_set_t *hash_set_new(size_t initial_buckets);

/* Returns non-zero if `key` is present (case-insensitive comparison). */
int hash_set_contains(const hash_set_t *hs, const char *key);

/* Inserts a copy of `key` into the set if not already present.
 * Returns 1 if the key was newly added, 0 if it already existed
 * (or on allocation failure treated as "already there" to be safe). */
int hash_set_add(hash_set_t *hs, const char *key);

/* Frees the set and all stored keys. Safe to call with NULL. */
void hash_set_free(hash_set_t *hs);

#endif
/* hashset.h ends here */
