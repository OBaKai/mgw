/*
 * Copyright (c) 2013 Hugh Bailey <obs.jim@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include "c99defs.h"

/*
 * Defines to make dynamic arrays more type-safe.
 * Note: Still not 100% type-safe but much better than using darray directly
 *       Makes it a little easier to use as well.
 *
 *       I did -not- want to use a gigantic macro to generate a crapload of
 *       typesafe inline functions per type.  It just feels like a mess to me.
 */
#ifdef __cplusplus
extern "C" {
#endif

struct darray {
	void *array;
	size_t num;
	size_t capacity;
};

#define DARRAY(type)                     \
	union {                          \
		struct darray da;        \
		struct {                 \
			type *array;     \
			size_t num;      \
			size_t capacity; \
		};                       \
	}

extern void darray_init(struct darray *dst);
#define da_init(v) darray_init(&v.da)

extern void darray_free(struct darray *dst);
#define da_free(v) darray_free(&v.da)

#define da_alloc_size(v) (sizeof(*v.array)*v.num)

extern void *darray_end(const size_t element_size,
		const struct darray *da);
#define da_end(v) darray_end(sizeof(*v.array), &v.da)

extern void darray_reserve(const size_t element_size,
		struct darray *dst, const size_t capacity);
#define da_reserve(v, capacity) \
	darray_reserve(sizeof(*v.array), &v.da, capacity)

extern void darray_resize(const size_t element_size,
		struct darray *dst, const size_t size);
#define da_resize(v, size) darray_resize(sizeof(*v.array), &v.da, size)

extern void darray_copy(const size_t element_size, struct darray *dst,
		const struct darray *da);
#define da_copy(dst, src)  \
	darray_copy(sizeof(*dst.array), &dst.da, &src.da)

extern void darray_copy_array(const size_t element_size,
		struct darray *dst, const void *array, const size_t num);
#define da_copy_array(dst, src_array, n) \
	darray_copy_array(sizeof(*dst.array), &dst.da, src_array, n)

extern void darray_move(struct darray *dst, struct darray *src);
#define da_move(dst, src) darray_move(&dst.da, &src.da)

size_t darray_find(const size_t element_size,
		const struct darray *da, const void *item, const size_t idx);
#define da_find(v, item, idx) \
	darray_find(sizeof(*v.array), &v.da, item, idx)

extern size_t darray_push_back(const size_t element_size,
		struct darray *dst, const void *item);
#define da_push_back(v, item) darray_push_back(sizeof(*v.array), &v.da, item)

extern void *darray_push_back_new(const size_t element_size,
		struct darray *dst);
#define da_push_back_new(v) darray_push_back_new(sizeof(*v.array), &v.da)

extern size_t darray_push_back_array(const size_t element_size,
		struct darray *dst, const void *array, const size_t num);
#define da_push_back_array(dst, src_array, n) \
	darray_push_back_array(sizeof(*dst.array), &dst.da, src_array, n)

extern size_t darray_push_back_darray(const size_t element_size,
		struct darray *dst, const struct darray *da);
#define da_push_back_da(dst, src) \
	darray_push_back_darray(sizeof(*dst.array), &dst.da, &src.da)

extern void darray_insert(const size_t element_size, struct darray *dst,
		const size_t idx, const void *item);
#define da_insert(v, idx, item) \
	darray_insert(sizeof(*v.array), &v.da, idx, item)

extern void *darray_insert_new(const size_t element_size,
		struct darray *dst, const size_t idx);
#define da_insert_new(v, idx) \
	darray_insert_new(sizeof(*v.array), &v.da, idx)

extern void darray_insert_array(const size_t element_size,
		struct darray *dst, const size_t idx,
		const void *array, const size_t num);
#define da_insert_array(dst, idx, src_array, n) \
	darray_insert_array(sizeof(*dst.array), &dst.da, idx, \
			src_array, n)

extern void darray_insert_darray(const size_t element_size,
		struct darray *dst, const size_t idx, const struct darray *da);
#define da_insert_da(dst, idx, src) \
	darray_insert_darray(sizeof(*dst.array), &dst.da, idx, \
			&src.da)

extern void darray_erase(const size_t element_size, struct darray *dst,
		const size_t idx);
#define da_erase(dst, idx) \
	darray_erase(sizeof(*dst.array), &dst.da, idx)

extern void darray_erase_item(const size_t element_size,
		struct darray *dst, const void *item);
#define da_erase_item(dst, item) \
	darray_erase_item(sizeof(*dst.array), &dst.da, item)

extern void darray_erase_range(const size_t element_size,
		struct darray *dst, const size_t start, const size_t end);
#define da_erase_range(dst, from, to) \
	darray_erase_range(sizeof(*dst.array), &dst.da, from, to)

extern void darray_pop_back(const size_t element_size,
		struct darray *dst);
#define da_pop_back(dst) \
	darray_pop_back(sizeof(*dst.array), &dst.da);

extern void darray_join(const size_t element_size, struct darray *dst,
		struct darray *da);
#define da_join(dst, src) \
	darray_join(sizeof(*dst.array), &dst.da, &src.da)

extern void darray_split(const size_t element_size, struct darray *dst1,
		struct darray *dst2, const struct darray *da, const size_t idx);
#define da_split(dst1, dst2, src, idx) \
	darray_split(sizeof(*src.array), &dst1.da, &dst2.da, \
			&src.da, idx)

extern void darray_move_item(const size_t element_size,
		struct darray *dst, const size_t from, const size_t to);
#define da_move_item(v, from, to) \
	darray_move_item(sizeof(*v.array), &v.da, from, to)

extern void darray_swap(const size_t element_size,
		struct darray *dst, const size_t a, const size_t b);
#define da_swap(v, idx1, idx2) \
	darray_swap(sizeof(*v.array), &v.da, idx1, idx2)

#ifdef __cplusplus
}
#endif