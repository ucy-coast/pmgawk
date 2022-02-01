/* Copyright (C) 2019 Terence Kelly.  All rights reserved.
   Minimalist generic persistent memory infrastructure:  Interface. */

#ifndef PMEM_H_INCLUDED
#define PMEM_H_INCLUDED

typedef uintptr_t pmo_t;  /* "persistent memory offset type" */

extern void * pmem_o2p(pmo_t o);
extern int    pmem_map(const char * const file);
extern pmo_t  pmem_alloc(size_t n);
extern int    pmem_unmap(void);
extern void   pmem_set_root(pmo_t o);
extern pmo_t  pmem_get_root(void);

#endif

