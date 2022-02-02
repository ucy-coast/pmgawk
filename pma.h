#ifndef PMA_H_INCLUDED
#define PMA_H_INCLUDED

extern int pma_init(const char * const file);
extern void pma_set_root(void* ptr);
extern void* pma_get_root(void);
extern void* pma_alloc(size_t size);
extern void* pma_calloc(size_t count, size_t size);
extern void* pma_realloc(void* ptr, size_t size);
extern void pma_free(void* ptr);

#endif

