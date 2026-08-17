#ifndef IRCD_HASH_H
#define IRCD_HASH_H

#include <stddef.h>

/**
 * One node in a separately chained hash-table bucket.
 *
 * Keys are owned by the table and copied when inserted.  Values are opaque
 * pointers owned by the caller unless a destructor is supplied to
 * hash_destroy().
 */
typedef struct HashEntry {
    char *key;                 /**< Heap-allocated lookup key. */
    void *value;               /**< Caller-owned value associated with key. */
    struct HashEntry *next;    /**< Next collision entry in this bucket. */
} HashEntry;

/**
 * Generic case-insensitive string hash table.
 *
 * Lookups use ASCII-style case-insensitive comparison.  This is sufficient
 * for the project's current foundation and can later be replaced with RFC
 * IRC casemapping without changing client/channel ownership.
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
