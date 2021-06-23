#include "bmem.h"
#include "base.h"
#include "threading.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

#define ALIGNMENT 32

static volatile long cnt_allocs = 0;

void *bmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr && !size)
        ptr = malloc(1);
    if (!ptr) {
        os_breakpoint();
		bcrash("Out of memory while trying to allocate %lu bytes",
				(unsigned long)size);
    }
    os_atomic_inc_long(&cnt_allocs);
    return ptr;
}

void *brealloc(void *ptr, size_t size)
{
    ptr = realloc(ptr, size);
    if (!ptr && !size)
        ptr = realloc(ptr, 1);
    if (!ptr) {
        os_breakpoint();
		bcrash("Out of memory while trying to allocate %lu bytes",
				(unsigned long)size);
    }

    os_atomic_inc_long(&cnt_allocs);
    return ptr;
}

void bfree(void *ptr)
{
    if (!!ptr) {
        os_atomic_dec_long(&cnt_allocs);
        free(ptr);
        ptr = NULL;
    }
}

long bcnt_allocs(void)
{
	return cnt_allocs;
}

void *bmemdup(const void *ptr, size_t size)
{
    void *out = bmalloc(size);
    if (size)
        memcpy(out, ptr, size);
    return out;
}

void bmem_init(struct bmem *mem)
{
    mem->array      = NULL;
    mem->len        = 0;
}

void bmem_free(struct bmem *mem)
{
    if (!!mem->array) {
        bfree(mem->array);
        mem->len = 0;
    }
}

void ensure_bmem_capacity(struct bmem *mem, size_t new_size)
{
    if (new_size <= mem->len) {
        memset(mem->array, 0, mem->len);
        return;
    }
    mem->array = (char*)brealloc(mem->array, new_size);
    mem->len = new_size;
}

void bmem_copy(struct bmem *mem, const char* array, size_t len)
{
    if (!array || !len) {
        bmem_free(mem);
        return;
    }
    ensure_bmem_capacity(mem, len);
    memcpy(mem->array, array, len);
    mem->len = len;
}

void bmem_copy_bmem(struct bmem *src_mem, struct bmem *dst_mem)
{
    if (dst_mem->array)
		bmem_free(dst_mem);

	if (src_mem->len) {
		ensure_bmem_capacity(dst_mem, src_mem->len);
		memcpy(dst_mem->array, src_mem->array, src_mem->len);
		dst_mem->len = src_mem->len;
	}
}

int base_get_alignment(void)
{
	return ALIGNMENT;
}