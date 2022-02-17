/* Copyright (C) 2019 Terence Kelly.  All rights reserved.
   Minimalist generic persistent memory infrastructure:  Implementation. */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "pma.h"

// Use (void) to silent unused warnings.
#define assertm(exp, msg) assert(((void)msg, exp))

#define S1(s) #s
#define S2(s) S1(s)
#define COORDS __FILE__ ":" S2(__LINE__) ": "
#define CK2(e, msg) do { if (!(e)) { fprintf(stderr, "%s failed: '%s' %s\n",  COORDS , #e, msg); abort(); } } while (0)
#define CK3(e, msg1, msg2) CK2(e, msg1 + " (" + msg2 + ")")
#define CK(e) CK2(e, "")
//#define FAIL(msg) do { std::cerr << COORDS << "failed: " << msg << std::endl; abort(); } while (0)

typedef uintptr_t pmo_t;  /* "persistent memory offset type" */

static_assert(sizeof(pmo_t) == sizeof(void *),     /* C11 */
              "offsets & pointers incompatible");

typedef struct {  /* header of backing file & in-memory image */
  pmo_t avail, end,  /* allocator bookkeeping */
        root;        /* live data must be reachable from root */
} pmh_s;  /* "persistent memory header structure" */

static pmh_s * e_base;  /* start address of in-memory image */
static size_t  e_len;   /* length of in-memory image */

#define UNIT (_Alignof(max_align_t))  /* C11 */
#define ALIGNED(o) (0 == (o) % UNIT)
#define ALIGN(o) do { while(! ALIGNED(o)) (o)++; } while (0)

/* Backing file and its in-memory image consist of a header (of
   type pmh_s above), a heap (nearly everything else), and final
   padding (one UNIT).  Padding at the high end eliminates an
   awkward corner case.  A root offset must "point" within the
   heap.  Other offsets (e.g., "end") may point one byte beyond
   heap, analogous to C rule for pointers (N1570 Sec 6.5.6). */
#define VALID(o) \
  (0 == (o) || (sizeof *e_base <= (o) && (o) <= e_len - UNIT))
#define VALID_ROOT(o) \
  (0 == (o) || (sizeof *e_base <= (o) && (o) <  e_len - UNIT))
#define VALID_PTR(ptr) \
  (NULL == (ptr) ||  \
   ((char*) e_base <= (char*) ptr) || \
   ((char*) ptr < (char*) e_base + e_len))

#define SANITY_CHECKS                                         \
  do {                                                        \
    assert((NULL == e_base && 0 == e_len) ||                  \
           (NULL != e_base && 0 != e_len) );                  \
    assert(NULL == e_base ||                                  \
           (  ALIGNED(e_base->avail) && ALIGNED(e_base->end)  \
             && VALID(e_base->avail) &&   VALID(e_base->end)  \
             && VALID_ROOT(e_base->root) ));                  \
  } while (0)

static void * pmem_o2p(pmo_t o) {  /* convert offset to pointer */
  assert(VALID(o));
  return 0 == o ? NULL : (char *)e_base + o;
}

static pmo_t pmem_p2o(void* ptr) {  /* convert pointer to offset */
  assert(VALID_PTR(ptr));
  return NULL == ptr ? 0 : (char *)ptr - (char*) e_base;
}

#define P2O(p) ((pmo_t)((char *)(p) - (char *)e_base))

#define RL return __LINE__  /* indicates where error occurs */

// Maps `file` at starting address `addr`
static int pmem_map(const char * const file, void* addr) {
  int fd, prot = PROT_READ | PROT_WRITE, flag = MAP_SHARED;
  long int pgsz;  struct stat sb;  size_t s;  pmh_s *t;
  SANITY_CHECKS;
  if (NULL != e_base)  /* limit: one mapping at a time */  RL;
  if (1 > (pgsz = sysconf(_SC_PAGESIZE)))                  RL;
  if (UNIT > (size_t)pgsz)                                 RL;
  if (0 > (fd = open(file, O_RDWR)))                       RL;
  if (0 != fstat(fd, &sb))                                 RL;
  if (10 * UNIT + sizeof *t > (s = (size_t)sb.st_size))    RL;
  if (0 != s % (unsigned long)pgsz)                        RL;
  if (MAP_FAILED ==
      (t = (pmh_s *)mmap(addr, s, prot, flag, fd, 0)))   {
    if (0 != close(fd))  /* don't leak fds ... */          RL;
    else                                                   RL; }
  if (0 != close(fd))                                    {
    if (0 != munmap(t, s))  /* ... or memory either */     RL;
    else                                                   RL; }
  /* file must be either new or already initialized: */
  if ( ! (   (0 == t->avail && 0 == t->end && 0 == t->root)
          || (0 != t->avail && 0 != t->end)))              RL;
  if (! (ALIGNED(t->avail) && ALIGNED(t->end)))            RL;
  e_base = t;
  e_len  = s;
  if (! (VALID(t->avail) && VALID(t->end)
         && VALID_ROOT(t->root)))                          RL;
  if (0 == t->avail) {  /* initialize persistent heap */
    t->avail = P2O(1 + t);
    ALIGN(t->avail);
    t->end   = P2O((char *)t + s - UNIT);
    t->root  = 0;
  }
  else  /* previously initialized; check size: */
    if (t->end != P2O((char *)t + s - UNIT))               RL;
  SANITY_CHECKS;
  return 0;
}

static pmo_t pmem_alloc(size_t n) {  /* "bump-pointer" allocator */
  pmo_t r;
  SANITY_CHECKS;
  assert(NULL != e_base);
  if (0 == n                             || /* ask 0, get 0 */
      e_base->avail     >= e_base->end   || /* out of p-mem */
      e_base->avail     >  ~(pmo_t)0 - n || /* "+n" overflows */
      e_base->avail + n >  e_base->end)     /* <n bytes left */
    return 0;
  r = e_base->avail;
  e_base->avail += n;
  ALIGN(e_base->avail);
  SANITY_CHECKS;
  return r;
}

static int pmem_unmap(void) {
  SANITY_CHECKS;
  if (NULL == e_base)              RL;
  if (0 != munmap(e_base, e_len))  RL;
  e_base = NULL;
  e_len = 0;
  return 0;
}

static void pmem_set_root(pmo_t o) {
  SANITY_CHECKS;
  assert(NULL != e_base && VALID_ROOT(o));
  e_base->root = o;
}

static pmo_t pmem_get_root(void) {
  SANITY_CHECKS;
  assert(NULL != e_base);
  return e_base->root;
}

void* find_gap_mmap(size_t L) {
  return mmap(NULL, L, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
}

int find_gap_munmap(void* A, size_t N) {
  return munmap(A, N);
}

// Finds a large gap in address space 
static int find_gap(void** addr, size_t* size) {
  int ret;
  void* A;
  void* Amax = NULL;
  size_t L = 0, U, Max=0;
  for (U=1;;U*=2) { /* double upper bound until failure */
    if (MAP_FAILED == (A = find_gap_mmap(U))) {
      break;
    } else {
      CK(find_gap_munmap(A, U)!=1);
    }
  }
  while (1+L<U) { /* binary search between bounds */
    size_t M = L + (U - L) / 2; /* avoid overflow */
    if (MAP_FAILED == (A = find_gap_mmap(M))) {
      U = M;
    } else {
      Amax = A;
      Max = M;
      CK(find_gap_munmap(A, M)!=1);
      L = M;
    }
  }
  *addr = Amax;
  *size = Max;
  return 0;
}

int pma_init(const char * const file) {
  int ret;
  int fd;
  const size_t pmem_size = 1024*1024*1024;

  // Create empty backing file if one doesn't exist
  if (access(file, F_OK) != 0) {
    if ((fd = open(file, O_CREAT|O_TRUNC|O_RDWR, S_IWUSR|S_IRUSR)) < 0) {
      ret = fd;
      goto done;  
    }
    if ((ret = ftruncate(fd, pmem_size)) < 0) {
      goto done;
    }  
  }
  
  void* start_addr;
  size_t size;
  CK(find_gap(&start_addr, &size) != 1);
  ret = pmem_map(file, start_addr);

done:
  return ret;
}

void pma_set_root(void* ptr) {
  pmo_t o = pmem_p2o(ptr);
  pmem_set_root(o);
}

void* pma_get_root(void) {
  pmo_t o = pmem_get_root();
  return pmem_o2p(o);
}

void* pma_alloc(size_t size) {
  pmo_t o = pmem_alloc(size);
  return pmem_o2p(o);
}

void* pma_calloc(size_t count, size_t size) {
  return pma_alloc(size * count);
}

void* pma_realloc(void* ptr, size_t size) {
  return pma_alloc(size);
}

void pma_free(void* ptr) {
  // does nothing as this simple bump-based allocator doesn't have a notion of freeing memory
}