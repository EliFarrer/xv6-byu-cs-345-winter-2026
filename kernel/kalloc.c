// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define BUF_SZ 7

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist; // linked list, protected by lock
  uint32 count;         // count of items in freelist, protected by lock
  char name[BUF_SZ];    // holds the name
};

struct kmem kmems[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    snprintf(kmems[i].name, BUF_SZ, "kmem_%d", i); // limited to six characters
    initlock(&kmems[i].lock, kmems[i].name);
    kmems[i].count = 0;
    kmems[i].freelist = 0;
  }
  freerange(end, (void*)PHYSTOP);
}

// frees all of memory and calls kfree (which puts it on the freelist)
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
  // printf("kfree\n");
  struct run *r;
  struct kmem *kmem;  // by pointer so we don't copy by value

  push_off();
  int cpu = cpuid();
  kmem = kmems + cpu;
  acquire(&kmem->lock); // push_off so it doesn't context switch in between. This keeps the cpu constant
  pop_off();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // prepends the pa onto the freelist
  r->next = kmem->freelist;  // set the start of the freelist to r->next
  kmem->freelist = r;        // set the freelist to r
  kmem->count++;
  release(&kmem->lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  struct kmem *kmem;  // kmem pointer so it changes the actual value, not creating a new one by value
  
  push_off();
  int cpu = cpuid();
  kmem = kmems + cpu;
  acquire(&kmem->lock);
  pop_off();
  
  r = kmem->freelist;
  if (!r) { // if we don't have memory, we need to steal
    if (steal(cpu, kmem)) { // if it returns 1
      r = kmem->freelist; // re-update r
    } else {
      // if it returns 0, no more memory
      release(&kmem->lock);
      return (void*)r;
    }
  }
  
  kmem->freelist = r->next;
  kmem->count--;

  release(&kmem->lock);

  memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// returns 1 if it works, 0 if it doesn't
int
steal(int cpu, struct kmem* kmem)
{
  struct kmem* other;
  int other_cpu_idx;
  int found_pages = 0;  // false

  release(&kmem->lock);

  while (!found_pages) {
    // three cases:
    // 1) no memory left, return 0
    // 2) we actually got pages, get the lock and exit
    // 3) we didn't get any pages after searching for the cpu, repeat things
    if ((other_cpu_idx = find_cpu_pages(cpu)) == -1) {
      // no memory left
      acquire(&kmem->lock);
      return 0;
    }
    other = kmems + other_cpu_idx;
    acquire(&other->lock);
    printf("steal: count=%d\n", other->count);
    if (other->count > 0) { // if we actually got pages
      found_pages = 1;
    } else {
      release(&other->lock);
    }
  }

  if (!move_stolen_pages(kmem, other)) {
    panic("steal: failed to move");
  }

  // printf("\tfrom pages new=%d, to pages new=%d\n", other->count, kmem->count);
  // keep the lock
  return 1;
}

// returns the cpu number with the most free pages, or -1 otherwise
// atomic
int
find_cpu_pages(int cpu)
{
  int max = 0;
  int max_idx = -1;
  
  for (int i = 0; i < NCPU; i++) {
    if (i == cpu) continue;
    if (kmems[i].count > max) {
      max = kmems[i].count;
      max_idx = i;
    }
  }
  return max_idx;
}

// assumes you have other->lock, releases it, then acquires kmem->lock without releasing
int
move_stolen_pages(struct kmem* kmem, struct kmem* other)
{
  /*
  Move the pages forward on other
  */
  struct run* last_page = 0;
  // int original_count = other->count;
  // If it is even, divide it, if it is odd, round up.
  // This handles the case when other->count is 1 so the loop will acutally run.
  // Otherwise, we will have a dangling page.
  int move_count = other->count % 2 == 0 ? other->count/2 : other->count/2 + 1;

  // printf("steal: stealing from=%d, to=%d\n", max_idx, cpu);
  // printf("steal: stealing %d\n", count);
  // printf("\tfrom pages og=%d, to pages og=%d\n", other->count, kmem->count);

  struct run* pages = other->freelist;
  for (int j = 0; j < count; j++) {
    if (j == count - 1) {
      last_page = other->freelist;
    }
    // printf("steal: other->freelist=%p, other->freelist->next=%p\n", other->freelist, other->freelist->next);
    other->freelist = other->freelist->next;  // get the next pointer     /* PROBLEM */
  }
  other->count = count;
  release(&other->lock);

  /*
  Move the pointers in the kmem struct
  */
  acquire(&kmem->lock);
  if (last_page == 0) {
    // no last page gotten
    return 0;
  }

  last_page->next = kmem->freelist;
  kmem->freelist = pages;
  kmem->count += original_count - count;  // to handle an odd count

  return 1;
}