#ifndef IRCD_TEXT_CACHE_H
#define IRCD_TEXT_CACHE_H

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define IRCD_TEXT_CACHE_MAX_BYTES 262144U

typedef struct TextFileCache {
    char *data;
    size_t length;
    char path[IRCD_CONFIG_PATH_MAX + 1U];
    time_t mtime_sec;
    long mtime_nsec;
    off_t file_size;
    int valid;
} TextFileCache;

static inline void text_file_cache_clear(TextFileCache *cache) {
    if (cache == NULL) return;
    free(cache->data);
    memset(cache, 0, sizeof(*cache));
}

/**
 * Return a bounded in-memory copy of path. stat(2) is used as a cheap change
 * detector; the file is reopened only when path, size, or nanosecond mtime
 * changes. Missing/oversized/unreadable files invalidate the cached value.
 */
static inline const char *text_file_cache_get(TextFileCache *cache,
                                               const char *path,
                                               size_t *length) {
    struct stat st;
    FILE *file;
    char *data;
    size_t wanted;
    size_t got;

    if (length != NULL) *length = 0U;
    if (cache == NULL || path == NULL || *path == '\0' ||
        strlen(path) > IRCD_CONFIG_PATH_MAX) return NULL;

    if (stat(path, &st) != 0 || st.st_size < 0 ||
        (uintmax_t)st.st_size > (uintmax_t)IRCD_TEXT_CACHE_MAX_BYTES) {
        text_file_cache_clear(cache);
        return NULL;
    }

    if (cache->valid && strcmp(cache->path, path) == 0 &&
        cache->file_size == st.st_size &&
        cache->mtime_sec == st.st_mtim.tv_sec &&
        cache->mtime_nsec == st.st_mtim.tv_nsec) {
        if (length != NULL) *length = cache->length;
        return cache->data;
    }

    wanted = (size_t)st.st_size;
    file = fopen(path, "rb");
    if (file == NULL) {
        text_file_cache_clear(cache);
        return NULL;
    }
    data = malloc(wanted + 1U);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }
    got = wanted != 0U ? fread(data, 1U, wanted, file) : 0U;
    if (ferror(file) || got != wanted) {
        free(data);
        fclose(file);
        text_file_cache_clear(cache);
        return NULL;
    }
    fclose(file);
    data[wanted] = '\0';

    free(cache->data);
    cache->data = data;
    cache->length = wanted;
    cache->file_size = st.st_size;
    cache->mtime_sec = st.st_mtim.tv_sec;
    cache->mtime_nsec = st.st_mtim.tv_nsec;
    cache->valid = 1;
    (void)snprintf(cache->path, sizeof(cache->path), "%s", path);
    if (length != NULL) *length = cache->length;
    return cache->data;
}

#endif
