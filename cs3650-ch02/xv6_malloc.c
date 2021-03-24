
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>

#include "xmalloc.h"

// Memory allocator by Kernighan and Ritchie,
// T  he C programming Language, 2nd ed.  Section 8.7.
//
// Then copied from xv6.
//
// Then modified to use mmap and add a mutex by Nat Tuck, becoming starter code
// for CS3650 in 2020.
//
// Note that this does not count as starter code for HW08, just for CH02.

typedef long Align;

union header
{
  struct
  {
    union header *ptr;
    unsigned int size;
  } s;
  Align x;
};

typedef union header Header;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Header base;
static Header *freep;

static void
xfree_helper(void *ap)
{
  Header *bp, *p;

  bp = (Header *)ap - 1;
  for (p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if (p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  if (bp + bp->s.size == p->s.ptr)
  {
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  }
  else
    bp->s.ptr = p->s.ptr;
  if (p + p->s.size == bp)
  {
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  }
  else
    p->s.ptr = bp;
  freep = p;
}

void xfree(void *ap)
{
  pthread_mutex_lock(&lock);
  xfree_helper(ap);
  pthread_mutex_unlock(&lock);
}

static Header *
morecore(size_t nu)
{
  char *p;
  Header *hp;

  if (nu < 4096)
    nu = 4096;
  // TODO: Replace sbrk use with mmap
  p = mmap(0, nu * sizeof(Header), PROT_READ | PROT_WRITE,
           MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (p == (char *)-1)
    return 0;
  hp = (Header *)p;
  hp->s.size = nu;
  xfree_helper((void *)(hp + 1));
  return freep;
}

void *
xmalloc(size_t nbytes)
{
  Header *p, *prevp;
  unsigned int nunits;

  pthread_mutex_lock(&lock);
  nunits = (nbytes + sizeof(Header) - 1) / sizeof(Header) + 1;
  if ((prevp = freep) == 0)
  {
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for (p = prevp->s.ptr;; prevp = p, p = p->s.ptr)
  {
    if (p->s.size >= nunits)
    {
      if (p->s.size == nunits)
        prevp->s.ptr = p->s.ptr;
      else
      {
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      pthread_mutex_unlock(&lock);
      return (void *)(p + 1);
    }
    if (p == freep)
    {
      if ((p = morecore(nunits)) == 0)
      {
        pthread_mutex_unlock(&lock);
        return 0;
      }
    }
  }
}

void *
xrealloc(void *prev, size_t nn)
{
  unsigned int nunits;

  pthread_mutex_lock(&lock);
  int prev_size = ((Header *)prev - 1)->s.size;
  nunits = (nn + sizeof(Header) - 1) / sizeof(Header) + 1;

  // calculate address we are looking for
  // and the size it must have to be used for realloc
  int looking_for_size = nunits - prev_size;
  Header *looking_for = (Header *)prev + prev_size;

  Header *p, *prevp;

  if ((prevp = freep) == 0)
  {
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for (p = prevp->s.ptr;; prevp = p, p = p->s.ptr)
  {
    if (p == looking_for && p->s.size == looking_for_size)
    {
      ((Header *)prev - 1)->s.size = nunits;
      p->s.size -= looking_for_size;
      p += looking_for_size;

      freep = prevp;
      pthread_mutex_unlock(&lock);
      return (void *)(prev);
      // if (p->s.size == nunits)
      //   prevp->s.ptr = p->s.ptr;
      // else
      // {
      //   p->s.size -= nunits;
      //   p += p->s.size;
      //   p->s.size = nunits;
      // }
      // freep = prevp;
      // pthread_mutex_unlock(&lock);
      // return (void *)(p + 1);
    }
  }

  // if we do not find the place we can realloc into,
  // create a new block and copy d
  void *new_block = xmalloc(nn);
  memcpy(new_block, prev, nn);
  xfree_helper(prev);
  return new_block;
}
