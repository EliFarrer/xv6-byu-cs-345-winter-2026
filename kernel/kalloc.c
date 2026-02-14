// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

/*
TODO:
- add a superinit() which will initialize the superpage space right before PHYSTOP (maybe 5 superpages?)
- create another spinlock/run struct for superpages
- edit kinit so it doesn't go all the way to PHYSTOP, but a few superpages before
- create a superkinit that goes from the end of kinit to PHYSTOP
- modify kfree so it doesn't let you free memory past PHYSTOP
- add constants
  - add a constant for the beginning of superpage memory or end of page memory (what was PHYSTOP)
  - add a constant for superpage size
  - add a constant that will bring you to the nearest superpage boundary

Questions:
- do I need another spinlock for the superpages, probably, but it can be separate because the two memory locations are separate

Part 2 (vm.c):
- How do we know if it is a superpage or a normal page? Is it just if the flag is PTE_V and PTE_R rather than just PTE_V?
- Do we just need to switch it to look for those flags in in uvmcopy and then deal with the sizes?

*/


#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
void freesuperrange(void *pa_start, void *pa_end);

// extern means it is defined somewhere else
// creates a char array
// the start of free virtual memory
extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// defining and instantiating a global struct called kmem, has a lock and a pointer to a run
struct pagelist {
  struct spinlock lock;
  struct run *freelist;
};

struct pagelist kmem;
struct pagelist superkmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");     // creates a spinlock
  freerange(end, (void*)SUPERPGSTART);   // free everything from after the kernel to SUPERPGSTART
  freesuperrange((void*)SUPERPGSTART, (void*)PHYSTOP);
}

// in the future go in and pass the kfree function in as a parameter and pass in a size. Then 
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start); // gets the start of the next page boundary
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) // starts at p and loops until the end (the p + PGSIZE makes it so it doesn't free one more after pa_end)
    kfree(p);   // because kfree prepends, the first page is actually right next to PHYSTOP
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  // checks that you passed it the start of a page || it is before the start || after the stop
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= SUPERPGSTART) {
    printf("pa: %p\n", pa);
    printf("end: %p\n", end);
    printf("superpgstart, %lx\n", SUPERPGSTART);
    panic("kfree");
  }
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  // prepends the current pagetable to the linked list
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
  if(r) // if it exists?
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk (5's)
  return (void*)r;  // returns a pointer to the start of the memory
}

void
superkfree(void *pa)
{
  // free the chunk of memory
  struct run *r;

  // checks that you passed it the start of a page || it is before the start || after the stop
  if(((uint64)pa % SUPERPGSIZE) != 0 || (char*)pa < (char*)SUPERPGSTART || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, SUPERPGSIZE);

  r = (struct run*)pa;

  acquire(&superkmem.lock);
  // prepends the current pagetable to the linked list
  r->next = superkmem.freelist;
  superkmem.freelist = r;
  release(&superkmem.lock);
}

void
freesuperrange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)SUPERPGROUNDUP((uint64)pa_start); // gets the start of the next page boundary
  for(; p + SUPERPGSIZE <= (char*)pa_end; p += SUPERPGSIZE) // starts at p and loops until the end (the p + SUPERPGSIZE makes it so it doesn't free one more after pa_end)
    superkfree(p);   // because kfree prepends, the first page is actually right next to PHYSTOP
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

void *
superkalloc(void)
{
  // allocate 2Mb chunk of memory
  // How do I make sure kfree doesn't walk over all of this?
  struct run *r;

  acquire(&superkmem.lock);
  r = superkmem.freelist;
  if(r) // if it exists?
    superkmem.freelist = r->next;
  release(&superkmem.lock);

  if(r)
    memset((char*)r, 6, SUPERPGSIZE); // fill with junk (6's)
  return (void*)r;  // returns a pointer to the start of the memory
}