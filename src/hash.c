#include "hash.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* djb2-style hash with lowercase folding for case-insensitive keys. */
static unsigned long hash_ci(const char *text) {
    unsigned long hash = 5381UL;
    unsigned char ch;

    while ((ch = (unsigned char)*text++) != 0U) {
        hash = ((hash << 5U) + hash) ^ (unsigned long)tolower(ch);
    }
    return hash;
}

/* Compare two NUL-terminated strings without regard to ASCII case. */
static int key_equal_ci(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

int hash_init(HashTable *table, size_t bucket_count) {
    if (table == NULL || bucket_count == 0U) {
        return -1;
    }

    table->bucket_count = bucket_count;
    table->buckets = calloc(bucket_count, sizeof(*table->buckets));
    return table->buckets != NULL ? 0 : -1;
}

void hash_destroy(HashTable *table, void (*free_value)(void *)) {
    size_t index;

    if (table == NULL || table->buckets == NULL) {
        return;
    }

    for (index = 0U; index < table->bucket_count; ++index) {
        HashEntry *entry = table->buckets[index];
        while (entry != NULL) {
            HashEntry *next = entry->next;
            free(entry->key);
            if (free_value != NULL) {
                free_value(entry->value);
            }
            free(entry);
            entry = next;
        }
    }

    free(table->buckets);
    table->buckets = NULL;
    table->bucket_count = 0U;
}

void *hash_get(const HashTable *table, const char *key) {
    size_t index;
    HashEntry *entry;

    if (table == NULL || table->buckets == NULL || key == NULL) {
        return NULL;
    }

    index = hash_ci(key) % table->bucket_count;
    for (entry = table->buckets[index]; entry != NULL; entry = entry->next) {
        if (key_equal_ci(entry->key, key)) {
            return entry->value;
        }
    }
    return NULL;
}

int hash_set(HashTable *table, const char *key, void *value) {
    size_t index;
    HashEntry *entry;

    if (table == NULL || table->buckets == NULL || key == NULL) {
        return -1;
    }

    index = hash_ci(key) % table->bucket_count;
    for (entry = table->buckets[index]; entry != NULL; entry = entry->next) {
        if (key_equal_ci(entry->key, key)) {
            entry->value = value;
            return 0;
        }
    }

    entry = calloc(1U, sizeof(*entry));
    if (entry == NULL) {
        return -1;
    }

    entry->key = strdup(key);
    if (entry->key == NULL) {
        free(entry);
        return -1;
    }

    entry->value = value;
    entry->next = table->buckets[index];
    table->buckets[index] = entry;
    return 0;
}

void *hash_remove(HashTable *table, const char *key) {
    size_t index;
    HashEntry **link;

    if (table == NULL || table->buckets == NULL || key == NULL) {
        return NULL;
    }

    index = hash_ci(key) % table->bucket_count;
    link = &table->buckets[index];

    while (*link != NULL) {
        HashEntry *entry = *link;
        if (key_equal_ci(entry->key, key)) {
            void *value = entry->value;
            *link = entry->next;
            free(entry->key);
            free(entry);
            return value;
        }
        link = &entry->next;
    }
    return NULL;
}
