#ifndef _BMEM_H_
#define _BMEM_H_

#include "c99defs.h"
#include <memory.h>
#include <wchar.h>

#ifdef __cplusplus
#define cpp_safe_delete(p) do {\
		delete (p); (p) = nullptr; \
	} while (false)
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct bmem {
    char        *array;
    size_t      len;
};


void *bmalloc(size_t size);
void *brealloc(void *ptr, size_t size);
void bfree(void *ptr);
long bcnt_allocs(void);

void *bmemdup(const void *ptr, size_t size);
void bmem_init(struct bmem *mem);
void bmem_free(struct bmem *mem);
void ensure_bmem_capacity(struct bmem *mem, size_t new_size);
void bmem_copy(struct bmem *mem, const char* array, size_t len);
void bmem_copy_bmem(struct bmem *src_mem, struct bmem *dst_mem);
EXPORT int base_get_alignment(void);

static inline void *bzalloc(size_t size)
{
	void *mem = bmalloc(size);
	if (mem)
		memset(mem, 0, size);
	return mem;
}

static inline char *bstrdup_n(const char *str, size_t n)
{
	char *dup;
	if (!str)
		return NULL;

	dup = (char*)bmemdup(str, n+1);
	dup[n] = 0;

	return dup;
}

static inline wchar_t *bwstrdup_n(const wchar_t *str, size_t n)
{
	wchar_t *dup;
	if (!str)
		return NULL;

	dup = (wchar_t*)bmemdup(str, (n+1) * sizeof(wchar_t));
	dup[n] = 0;

	return dup;
}

static inline char *bstrdup(const char *str)
{
	if (!str)
		return NULL;

	return bstrdup_n(str, strlen(str));
}

static inline wchar_t *bwstrdup(const wchar_t *str)
{
	if (!str)
		return NULL;

	return bwstrdup_n(str, wcslen(str));
}

#ifdef __cplusplus
}
#endif
#endif
