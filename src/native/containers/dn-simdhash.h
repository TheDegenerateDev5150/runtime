// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#ifndef __DN_SIMDHASH_H__
#define __DN_SIMDHASH_H__

#include <stdint.h>
#include "dn-utils.h"
#include "dn-allocator.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// We reserve the last two bytes of each suffix vector to store data
#define DN_SIMDHASH_MAX_BUCKET_CAPACITY 14
// The ideal capacity depends on the size of your keys. For 4-byte keys, it is 12.
#define DN_SIMDHASH_DEFAULT_BUCKET_CAPACITY 12
// We use the last two bytes specifically to store item count and cascade flag
#define DN_SIMDHASH_COUNT_SLOT (DN_SIMDHASH_MAX_BUCKET_CAPACITY)
// The cascade flag indicates that an item overflowed from this bucket into the next one
#define DN_SIMDHASH_CASCADED_SLOT (DN_SIMDHASH_MAX_BUCKET_CAPACITY + 1)
// We always use 16-byte-wide vectors (I've tested this, 32-byte vectors are slower)
#define DN_SIMDHASH_VECTOR_WIDTH 16
// Set a minimum number of buckets when created, regardless of requested capacity
#define DN_SIMDHASH_MIN_BUCKET_COUNT 1
// User-specified capacity values will be increased to this percentage in order
//  to maintain an ideal load factor. FIXME: 120 isn't right
#define DN_SIMDHASH_SIZING_PERCENTAGE 130
// If set, bucket count will be a power of two. If unset, we will use spaced primes.
// Spaced primes give much better collision resistance for bad hashes, but worsen perf for optimal hashes.
#define DN_SIMDHASH_POWER_OF_TWO_BUCKETS 0

typedef struct dn_simdhash_void_data_t {
	// HACK: Empty struct or 0-element array produce a MSVC warning and break the build.
	uint8_t data[1];
} dn_simdhash_void_data_t;

typedef struct dn_simdhash_buffers_t {
	// sizes of current allocations in items (not bytes)
	// so values_length should == (buckets_length * bucket_capacity)
	uint32_t buckets_length, values_length,
	// The number of bytes we pushed the buckets ptr forward after allocating it.
	// We'll need to subtract this from the ptr before freeing.
		buckets_bias;
	void *buckets;
	void *values;
	dn_allocator_t *allocator;
} dn_simdhash_buffers_t;

typedef struct dn_simdhash_t dn_simdhash_t;

typedef struct dn_simdhash_meta_t {
	// type metadata for generic implementation
	// NOTE: key_size and value_size are not used consistently by every part of the implementation,
	//  a specialization is still strongly typed based on its KEY_T and VALUE_T. But they need to match.
	uint32_t bucket_capacity, bucket_size_bytes, key_size, value_size,
	// Allocate this many bytes of extra data inside the dn_simdhash_t
		data_size;
} dn_simdhash_meta_t;

typedef enum dn_simdhash_insert_mode {
	// Ensures that no matching key exists in the hash, then adds the key/value pair
	DN_SIMDHASH_INSERT_MODE_ENSURE_UNIQUE,
	// If a matching key exists in the hash, overwrite its value but leave the key alone
	DN_SIMDHASH_INSERT_MODE_OVERWRITE_VALUE,
	// If a matching key exists in the hash, overwrite both the key and the value
	DN_SIMDHASH_INSERT_MODE_OVERWRITE_KEY_AND_VALUE,
	// Do not scan for existing matches before adding the new key/value pair.
	DN_SIMDHASH_INSERT_MODE_REHASHING,
} dn_simdhash_insert_mode;

typedef enum dn_simdhash_add_result {
	DN_SIMDHASH_INTERNAL_ERROR = -2,
	DN_SIMDHASH_OUT_OF_MEMORY = -1,
	DN_SIMDHASH_ADD_FAILED = 0,
	DN_SIMDHASH_ADD_INSERTED = 1,
	DN_SIMDHASH_ADD_OVERWROTE = 2,
} dn_simdhash_add_result;

typedef enum dn_simdhash_insert_result {
	DN_SIMDHASH_INSERT_OK_ADDED_NEW,
	DN_SIMDHASH_INSERT_OK_OVERWROTE_EXISTING,
	DN_SIMDHASH_INSERT_NEED_TO_GROW,
	DN_SIMDHASH_INSERT_KEY_ALREADY_PRESENT,
} dn_simdhash_insert_result;

typedef struct dn_simdhash_vtable_t {
	// Does not free old_buffers, that's your job. Required.
	void (*rehash) (dn_simdhash_t *hash, dn_simdhash_buffers_t old_buffers);
	// Invokes remove handler for all items, if necessary. Optional.
	void (*destroy_all) (dn_simdhash_t *hash);
} dn_simdhash_vtable_t;

typedef struct dn_simdhash_t {
	// internal state
	uint32_t count, grow_at_count;
	dn_simdhash_buffers_t buffers;
	dn_simdhash_vtable_t vtable;
	dn_simdhash_meta_t *meta;
	// We allocate extra space here based on meta.data_size
	// This has one element because 0 elements generates a MSVC warning and breaks the build
	uint8_t data[1];
} dn_simdhash_t;

#define dn_simdhash_instance_data(type, hash) \
	(*(type *)(&hash->data))

// These helpers use .values instead of .vec to avoid generating unnecessary
//  vector loads/stores. Operations that touch these values may not need vectorization,
//  so it's ideal to just do single-byte memory accesses instead.
// These unfortunately have to be macros because the suffixes type isn't defined yet
#define dn_simdhash_bucket_count(suffixes) \
	(suffixes).values[DN_SIMDHASH_COUNT_SLOT]

#define dn_simdhash_bucket_cascaded_count(suffixes) \
	(suffixes).values[DN_SIMDHASH_CASCADED_SLOT]

#define dn_simdhash_bucket_set_suffix(suffixes, slot, value) \
	(suffixes).values[(slot)] = (value)

#define dn_simdhash_bucket_set_count(suffixes, value) \
	(suffixes).values[DN_SIMDHASH_COUNT_SLOT] = (value)

#define dn_simdhash_bucket_set_cascaded_count(suffixes, value) \
	(suffixes).values[DN_SIMDHASH_CASCADED_SLOT] = (value)

static DN_FORCEINLINE(uint8_t)
dn_simdhash_select_suffix (uint32_t key_hash)
{
	// Extract low 8 bits and ensure that the suffix isn't 0.
	// The lowest bits of the hash are used to select the bucket index.
	uint8_t result = key_hash & 0xFF;
	// F14 uses a bitwise or, but this will compile down to a cmov which is (in testing) typically just as good,
	//  and gives us nearly twice as many possible suffixes.
	return result ? result : 0xFF;
}

// This relies on bucket count being a power of two.
#if DN_SIMDHASH_POWER_OF_TWO_BUCKETS
#define dn_simdhash_select_bucket_index(buffers, key_hash) \
	((key_hash) & ((buffers).buckets_length - 1))
#else
#define dn_simdhash_select_bucket_index(buffers, key_hash) \
	((key_hash) % ((buffers).buckets_length))
#endif

// Creates a simdhash with the provided configuration metadata, vtable, size, and allocator.
// Be sure you know what you're doing.
dn_simdhash_t *
dn_simdhash_new_internal (dn_simdhash_meta_t *meta, dn_simdhash_vtable_t vtable, uint32_t capacity, dn_allocator_t *allocator);

// Frees a simdhash and its associated buffers.
void
dn_simdhash_free (dn_simdhash_t *hash);

// Frees a set of simdhash buffers (returned by ensure_capacity_internal).
void
dn_simdhash_free_buffers (dn_simdhash_buffers_t buffers);

// If a resize happens, this will allocate new buffers and return the old ones.
// It is your responsibility to rehash and then free the old buffers.
// If growing failed due to an out of memory condition *ok will be 0, otherwise it will be 1.
dn_simdhash_buffers_t
dn_simdhash_ensure_capacity_internal (dn_simdhash_t *hash, uint32_t capacity, uint8_t *ok);

// Erases the contents of the table, but does not shrink it.
void
dn_simdhash_clear (dn_simdhash_t *hash);

// Returns the actual number of values the table can currently hold.
// It may grow automatically before reaching that point.
uint32_t
dn_simdhash_capacity (dn_simdhash_t *hash);

// Returns the number of value currently stored in the table.
uint32_t
dn_simdhash_count (dn_simdhash_t *hash);

// Returns the estimated number of items that have overflowed out of a bucket.
// WARNING: This is expensive to calculate.
uint32_t
dn_simdhash_overflow_count (dn_simdhash_t *hash);

// Automatically resizes the table if it is too small to hold the requested number
//  of items. Will not shrink the table if it is already bigger. Returns whether
//  the operation was successful - 0 indicates allocation failure.
uint8_t
dn_simdhash_ensure_capacity (dn_simdhash_t *hash, uint32_t capacity);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // __DN_SIMDHASH_H__
