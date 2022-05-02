/* "pma", a persistent memory allocator (interface)
   Copyright (C) 2022  Terence Kelly
   Contact:  tpkelly @ { acm.org, cs.princeton.edu, eecs.umich.edu }

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Affero General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef PMA_H_INCLUDED
#define PMA_H_INCLUDED

// version strings of interface and implementation should match
#define PMA_H_VERSION "2022.05May.01 (Avon 5)"
extern const char pma_version[];

/* May contain line number in pma.c where something went wrong if one
   of the other interfaces fails.  "Use the Source, Luke." */
extern int pma_errno;

/* Must be called exactly once before any other pma_* function is
   called, otherwise behavior is undefined.  Available verbosity
   levels are: 0 => no diagnostic printouts, 1 => errors only printed
   to stderr, 2 => also warnings, 3 => also very verbose "FYI"
   messages.  Use verbosity level 2 by default.  File argument
   specifies backing file containing persistent heap, initially an
   empty sparse file whose size is a multiple of the system page
   size.  If file argument is NULL, fall back on standard memory
   allocator: pma_malloc passes the buck to ordinary malloc, pma_free
   calls ordinary free, etc.  In fallback-to-standard-malloc mode,
   only pma_malloc/calloc/realloc/free may be used; other functions
   such as pma_get/set_root must not be used.  The implementation may
   store a copy of the file argument, i.e., the pointer, so both this
   pointer and the memory to which it points (*file) must not
   change. */
extern int pma_init(int verbose, const char *file);

/* The following four functions may be used like their standard
   counterparts.  See notes on pma_init for fallback to standard
   allocator.  See notes on fork in README. */
extern void * pma_malloc(size_t size);
extern void * pma_calloc(size_t nmemb, size_t size);
extern void * pma_realloc(void *ptr, size_t size);
extern void   pma_free(void *ptr);

/* The following two functions access the persistent heap's root
   pointer, which must either be NULL or must point within a block of
   persistent memory.  If the pointer passed to pma_set_root is not a
   pointer returned by pma_malloc/calloc/realloc, that is likely a
   bug, though the implementation is not obliged to check for such
   bugs. */
extern void   pma_set_root(void *ptr);
extern void * pma_get_root(void);

/* Prints to stderr details of internal data structures, e.g., free
   lists, and performs integrity checks, which may be very slow. */
extern void pma_check_and_dump(void);

/* Set non-metadata words of free blocks to given value.  Useful for
   debugging (v == 0xdeadDEADbeefBEEF) and for preparing backing file
   to be re-sparsified with fallocate (v == 0x0). */
extern void pma_set_avail_mem(const unsigned long v);

#endif // PMA_H_INCLUDED
