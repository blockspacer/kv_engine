/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#pragma once

/**
 * Use this file as an abstraction to the underlying hooks api
 */

#include <stdint.h>
#include <stdlib.h>
#include <vector>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct allocator_ext_stat {
    char key[48];
    size_t value;
} allocator_ext_stat;

typedef struct allocator_stats {
    /* Bytes of memory allocated by the application. Doesn't include allocator
       overhead or fragmentation. */
    size_t allocated_size;

    /* Bytes of memory reserved by the allocator */
    size_t heap_size;

    /* mem occupied by allocator metadata */
    size_t metadata_size;

    /* Memory overhead of the allocator*/
    size_t fragmentation_size;

    /* memory that has not been given back to the OS */
    size_t retained_size;

    /* max bytes in resident pages mapped by the allocator*/
    size_t resident_size;

    /* Vector of additional allocator-specific statistics */
    std::vector<allocator_ext_stat> ext_stats;

} allocator_stats;

/**
 * Engine allocator hooks for memory tracking.
 */
struct ServerAllocatorIface {
    /**
     * Add a hook into the memory allocator that will be called each
     * time memory is allocated from the heap. Returns true if the
     * hook was successfully registered with the allocator. Returns
     * false if the hook was not registered properly or if a hooks
     * API doesn't exist for the allocator in use.
     */
    bool (*add_new_hook)(void (*)(const void* ptr, size_t size));

    /**
     * Remove a hook from the memory allocator that will be called each
     * time memory is allocated from the heap. Returns true if the hook
     * was registered and removed and false if the specified hook is not
     * registered or if a hooks API doesn't exist for the allocator.
     */
    bool (*remove_new_hook)(void (*)(const void* ptr, size_t size));

    /**
     * Add a hook into the memory allocator that will be called each
     * time memory is freed from the heap. Returns true if the hook
     * was successfully registered with the allocator. Returns false
     * if the hook was not registered properly or if a hooks API
     * doesn't exist for the allocator in use.
     */
    bool (*add_delete_hook)(void (*)(const void* ptr, size_t size));

    /**
     * Remove a hook from the memory allocator that will be called each
     * time memory is freed from the heap. Returns true if the hook was
     * registered and removed and false if the specified hook is not
     * registered or if a hooks API doesn't exist for the allocator.
     */
    bool (*remove_delete_hook)(void (*)(const void* ptr, size_t size));

    /**
     * Returns the number of extra stats for the current allocator.
     */
    int (*get_extra_stats_size)(void);

    /**
     * Obtains relevant statistics from the allocator. Every allocator
     * is required to return total allocated bytes, total heap bytes,
     * total free bytes, and toal fragmented bytes. An allocator will
     * also provide a varying number of allocator specific stats
     */
    void (*get_allocator_stats)(allocator_stats*);

    /**
     * Returns the total bytes allocated by the allocator for the allocated
     * memory pointed to by 'ptr'.
     * This value may be computed differently based on the allocator in use.
     */
    size_t (*get_allocation_size)(const void* ptr);

    /**
     * Returns the total bytes allocated by the allocator for a request of
     * size 'sz'.
     * Returns 0 if the given allocator cannot determine the total size from
     * the requested size (i.e. one must use get_allocation_size().
     *
     * For allocators which support it (e.g. jemalloc) this is faster than
     * get_allocation_size() as it doesn't require looking up a particular
     * pointer.
     */
    size_t (*get_allocation_size_from_sz)(size_t sz);

    /**
     * Fills a buffer with special detailed allocator stats.
     */
    void (*get_detailed_stats)(char*, int);

    /**
     * Attempts to release free memory back to the OS.
     */
    void (*release_free_memory)(void);

    /**
     * Enables / disables per-thread caching by the allocator
     * __for the calling thread__. Returns if the thread cache was enabled
     * before the call.
     */
    bool (*enable_thread_cache)(bool enable);

    /**
     * Gets a property by name from the allocator.
     * @param name property name
     * @param value destination for numeric value from the allocator
     * @return whether the call was successful
     */
    bool (*get_allocator_property)(const char* name, size_t* value);
};

#ifdef __cplusplus
}
#endif
