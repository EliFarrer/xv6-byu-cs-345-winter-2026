// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// Reference counts to all pages
// max of 64 processes, so we have an array of bytes
// REFCOUNT: keep refcounts
// Need a lock on this because anyone can modify it.
struct {
  struct spinlock lock;
  unsigned char refcounts[(PHYSTOP-KERNBASE)/PGSIZE];
} refs;

void
refincrement_lock(void* pa) {
  acquire(&refs.lock);
  refincrement(pa);
  release(&refs.lock);
}

void
refincrement(void* pa) {
  // verify pa is on a page boundary
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP) {
    printf("pa: %p\n", pa);
    panic("kfree page address refincrement");
  }

  uint64 idx = ((uint64)pa - KERNBASE) / PGSIZE;
  refs.refcounts[idx]++;
}

void
refdecrement_lock(void* pa) {
  acquire(&refs.lock);
  refdecrement(pa);
  release(&refs.lock);
}

void
refdecrement(void* pa) {
  // verify pa is on a page boundary
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree page address refdecrement");

  uint64 idx = ((uint64)pa - KERNBASE) / PGSIZE;
  refs.refcounts[idx]--;
}

unsigned char refidx_lock(void* pa) {
  acquire(&refs.lock);
  unsigned char count = refidx(pa);
  release(&refs.lock);
  return count;
}

unsigned char
refidx(void* pa) {
  // verify pa is on a page boundary
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree page address refidx");

  uint64 idx = ((uint64)pa - KERNBASE) / PGSIZE;
  return refs.refcounts[idx];
}

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&refs.lock, "refs"); // init the lock before we call kfree in freerange which requires the lock and sets everything to 1's
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  // check to make sure the address is valid
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // REFCOUNT: verify the page reference count is 0 before putting it back on the freelist
  acquire(&refs.lock);
  if (refidx(pa) >= 1) {
    refdecrement(pa);   // decrement the reference
  }
  if (refidx(pa) > 0) {  // if it is not 0, just return
    release(&refs.lock);
    return;
  }
  // if it is 0, continue and free the page
  release(&refs.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
  
  // REFCOUNT: set initial reference count
  if (r) {
    acquire(&refs.lock);
    refincrement(r);
    release((&refs.lock));
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}
