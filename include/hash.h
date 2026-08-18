#ifndef IRCD_HASH_H
#define IRCD_HASH_H

#include <stddef.h>

/** One node in a separately chained hash-table bucket. */
typedef struct HashEntry {
    char *key;                 /**< Heap-allocated lookup key. */
    void *value;               /**< Caller-owned value associated with key. */
    struct HashEntry *next;    /**< Next collision entry in this bucket. */
} HashEntry;

/**
 * Generic IRC string hash table.
 *
 * Keys are hashed and compared with RFC1459 casemapping.  In addition to
 * ASCII A-Z folding, []\\^ and {}|~ are treated as case-equivalent pairs.
 * This is the lookup behavior advertised by CASEMAPPING=rfc1459 and is used
 * for both nicknames and channel names.
 */
typedef struct HashTable {
    size_t bucket_count;       /**< Number of entries in buckets[]. */
    HashEntry **buckets;       /**< Array of linked-list bucket heads. */
} HashTable;

/** Allocate the bucket array for an empty table. Returns 0 or -1 on failure. */
int hash_init(HashTable *table, size_t bucket_count);

/** Destroy all entries and optionally invoke free_value for every value. */
void hash_destroy(HashTable *table, void (*free_value)(void *));

/** Return the value for key, or NULL when the key is absent. */
void *hash_get(const HashTable *table, const char *key);

/** Insert or replace key with value. Returns 0 or -1 on allocation failure. */
int hash_set(HashTable *table, const char *key, void *value);

/** Remove key and return its value without destroying that value. */
void *hash_remove(HashTable *table, const char *key);

#endif /* IRCD_HASH_H */
