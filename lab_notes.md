# Locks Lab
Prints the number of loop iterations it took to acquire a specific lock.

Have one freelist per cpu with its own lock. Need to handle stealing when empty.

The current lock protecting the freelist is `kmem.lock` called `kmem`

## Tests to run
`kalloctest`: load

`usertests sbrkmuch`: tests that all of memory is still available

`usertests` should all pass `make grade` should also work

## Plan
### Modify `kinit()`
In `kinit()`, iterate over all the cpu's and initialize a lock with a name based on the cpu number. I can use `snprintf()` for this. Format like: `kmem_<CPU_ID>`.

### Modify `kmem` struct
Use the `kmem` struct as an element in a static list of length `NCPU`. Each element is one of these structs and has a lock and freelist. What should I call this?
From:
```
struct {
  struct spinlock lock;
  struct run *freelist; // linked list
} kmem;
```
To:
```
struct kmem {
  struct spinlock lock;
  struct run *freelist; // linked list, protected by spinlock
  uint32 count;         // size of the freelist, protected by spinlock
};
struct kmem kmems[NCPU];
```

### Modify `kalloc()`
Get cpuid with `cpuid()`. Disable and reenable interrupts with `push_off()` and `pop_off()`.

Lock the freelist for the `cpu`. Get the freelist.

Check the freelist. If there is nothing left in the freelist, call `steal()`.
- while `steal` returns 1:
    - check to make sure the `freelist` has pages
        - if it does, break
- if steal returns 0, we are out of memory, return 0.

Decrement `count` if successful

Release lock

### Modify `kfree()`
Get cpuid with `cpuid()`. Disable and reenable interrupts with `push_off()` and `pop_off()`.

Lock the freelist for the current cpu.

Add back to the current cpu's `freelist`.

Increment `count`.

Release lock.

### Add `int steal(int cpu, struct kmem* kmem)`
Release the spinlock for `kmem`.

Stealing will only be called if a cpu's freelist is empty. There will be a check to skip the cpu that it is checking in case more pages are added in between the time it is called and the time it checks.

Depends on the current CPU's kmem lock not being held.

`int steal(int cpu, struct kmem* kmem)`

Iterates through the `kmems` struct looking for the biggest freelist (from `count`). Biggest freelist is called `other`.

Locks `other`'s freelist and then moves half of the pages out from `other`. Updates `count`. Unlocks `other`'s freelist. Grab's `kmem`'s freelist lock and moves the pages into its freelist. Updates `count`.

Returns 0 on failure: there are no pages left, 1 on success: there are pages left and it successfully stole them.

Get the original spinlock for `kmem`.

## Questions
If I don't lock any of the freelists when I check them, isn't it possible that in between checking for the counts and stealing the pages, the pages are put to use and I get an error?

To solve this, we just have to check again. I will use a while loop to say that while I keep getting 0 from `steal()`, then break if I actually get a valid freelist.

## Tips
Initially, have one CPU call `freerange` and don't try to split it up by cpu. They will just steal instead.

`NCPU` is the number of CPU's (8).

`cpuid` will get the current core number. `push_off()` will push no interrupts on the list, and `pop_off()` will pop one off. Defined in `spinlock.c`

`snprintf()` for string formatting.

Steal from largest, linear search, no locking.

See also the race detector.
