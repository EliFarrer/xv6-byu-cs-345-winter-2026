#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "file.h"
#include "fcntl.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
      p->next_vma = VMAS;
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    printd("allocproc: calling freeproc 1\n");
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    printd("allocproc: calling freeproc 2\n");
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable) {
    proc_freepagetable(p->pagetable, p->sz);
  }
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    printd("fork: calling freeproc\n");
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      intr_on();
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

uint64
proc_mmap(uint64 addr, size_t len, int prot, int flags, int fd, off_t offset)
{
  struct file *f;
  vma_t *vma;
  uint64 start;
  struct proc *proc = myproc();
  printd("proc_mmap: starting\n");

  if (len < 0) {
    printd("proc_mmap: bad length\n");
    return 0;
  }

  if(fd < 0 || fd >= NOFILE || (f=proc->ofile[fd]) == 0) {
    printd("proc_mmap: bad fd\n");
    return 0;
  }

  if (!(f->writable) && (flags & MAP_SHARED)) {
    printd("proc_mmap: can't write when MAP_SHARED\n");
    return 0;
  }

  printd("proc_mmap: increment ref count\n");
  filedup(f); // increment reference count

  // the math for the start and the end
  printd("proc_mmap: get start and end\n");
  start = proc->next_vma - (PGROUNDUP(len));
  vma = proc_vma_alloc(start, len, prot, flags, f, proc);
  proc->next_vma = start - PGSIZE;

  // allocate and map the memory
  printd("proc_mmap: returning\n");
  return vma->start;
}

int
proc_munmap(uint64 addr, size_t len)
{
  struct vma_t *vma;
  pte_t *pte;
  int npages = PGROUNDUP(len) / PGSIZE;
  int write_back = 0;
  // int f_offset;
  
  if ((len % PGSIZE) != 0) {
    printd("len not page aligned\n");
    return -1;
  }

  if ((vma = get_proc_vma_from_addr(addr)) == 0) {
    printd("proc_munmap: could not find the vma with the addr\n");
    return -1; // find the vma
  }
  if (vma_contains(vma, addr) != 1) {
    printd("proc_munmap: found vma does not contain addr\n");
    return -1;  // check that the given region is actuall contained within the actual mapping of the vma
  }

  // f_offset = vma->offset + (PGROUNDDOWN(addr) - vma->start);

  for (int i = 0; i < len; i += PGSIZE) {
    if ((pte = walk(myproc()->pagetable, addr + i, 0)) == 0) {
      printd("proc_munmap: pte doesn't exist\n");
      return -1;  // pte doesn't exist
    }

    write_back = min(PGSIZE, vma->len - (addr - vma->start));
    write_back = min(write_back, vma->file->ip->size - vma->offset);
    vma_print(vma);

    if ((*pte & PTE_D) && (vma->flags & MAP_SHARED) && (vma->prot & PROT_WRITE)) {
      begin_op();
      ilock(vma->file->ip);
      printd("proc_munmap: writing at file offset %lx\n", vma->offset);
      printd("proc_munmap: writing amount %x\n", write_back);
      int written = writei(vma->file->ip, 0, PTE2PA(*pte), vma->offset, write_back); // filewrite keeps track of an offset, writei doesn't
      if (written != write_back) {
        printd("written was not as was expected\n");
        return -1;
      }
      iunlock(vma->file->ip);
      end_op();
    }
    // adjust vma bounds
    if ((vma->start) == addr + i) { // check if we unmap from the start
      printd("proc_munmap: updating start\n");
      vma->start += write_back;
      vma->offset += write_back;
      vma->len -= write_back;
    } else if ((vma->start + vma->len) == (addr + len)) {  // check if we unmap from the end
      printd("proc_munmap: updating end\n");
      vma->len -= len;
    } else {
      panic("unhandled range in proc_munmap");
    }
    vma_print(vma);
  }

  // free pages and remove from pagetable
  uvmunmap(myproc()->pagetable, addr, npages, 1);
  sfence_vma();
  
  if (vma->len == 0) {
    fileclose(vma->file);
    proc_vma_dealloc(myproc(), vma);
  }
  return 0;
}

// sets the vma in the struct process and calls vma_alloc to find an open global vma
vma_t*
proc_vma_alloc(uint64 start, size_t len, int prot, int flags, struct file* f, struct proc* proc)
{
  vma_t *vma;
  int found = 0;
  printd("proc_vma_alloc: starting\n");
  for (int i = 0; i < NVMAS; i++) {
    if (proc->proc_vmas[i] == 0){ // vma holder is open
      vma = vma_alloc(start, len, prot, flags, f);
      proc->proc_vmas[i] = vma;
      found = 1;
      break;
    }
  }
  if (!found) {
    panic("No open vma found in the process");
  }

  return vma;
}

int
proc_vma_dealloc(struct proc* proc, vma_t *vma)
{
  printd("proc_vma_dealloc: starting\n");
  int found = 0;
  vma_dealloc(vma);
  // printd("cleared vma:\n");
  // vma_print(vma);
  for (int i = 0; i < NVMAS; i++) {
    if (proc->proc_vmas[i] == vma){ // if we found the vma
      proc->proc_vmas[i] = 0;
      found = 1;
      break;
    }
  }
  if (!found) {
    panic("No vma found in the process to deallocate");
  }

  return 0;
}

// does basic sanity checking after trapping
int
mmapfaultchecker(pagetable_t pagetable, uint64 pageva, uint64 scause) {
  printd("mmapfaultchecker: starting\n");
  pte_t *pte;
  struct vma_t* vma;

  if(pageva >= MAXVA) {
    printd("mmapfaultchecker: pageva outside valid range\n");
    return -1;  // if the page is outside of the valid range
  }

  if(walkaddr(pagetable, pageva) != 0) {
    printd("mmapfaultchecker: %lx address already mapped\n", pageva);
    return -1;  // if it is alread mapped to physical memory, bad access of address
  }

  if ((vma = get_proc_vma_from_addr(pageva)) == 0) {
    printd("mmapfaultchecker: no vma has the address\n");
    return -3;  // no vma mapping exists for that address
  }

  pte = walk(pagetable, pageva, 0);
  return mmapfaulthandler(pagetable, pte, pageva, vma, scause); // if it is not a mmap page, then we are trying to write to bad memory
}

// returns 0 if it was handled
// returns -1 otherwise
int
mmapfaulthandler(pagetable_t pagetable, pte_t* pte, uint64 pageva, struct vma_t* vma, uint64 scause) {
  // if it is a read fault, the page has not ever been allocated (because anything can read if that flag is set)
  // if we are trying to write to a MAP_PRIVATE, we need to create a copy
  // MAP_SHARED will only get here if it is the first time, in which case we need to create a copy
  // basically no matter what, we will copy something if it gets here.
  printd("mmapfaulthandler: starting\n");
  struct inode *ip = vma->file->ip;
  char *mem;
  int read;
  uint64 f_offset;

  // allocate a page
  if((mem = kalloc()) == 0) {
    return -4;
  }
  memset(mem, 0, PGSIZE); // zero out

  printd("mmapfaulthandler: vma offset: %lx\n", vma->offset);
  printd("mmapfaulthandler: pageva: %lx\n", pageva);
  printd("mmapfaulthandler: vma start: %lx\n", vma->start);
  f_offset = vma->offset + (pageva - vma->start);
  ilock(ip);
  
  // readi
  read = readi(ip, 0, (uint64)mem, f_offset, PGSIZE);
  if ((read > PGSIZE) || (read < 0)) {  // virtual address, offset 0, PGSIZE bytes
    panic("readi failed > PGSIZE");
  }

  iunlock(ip);  // releases the lock from the read data, we don't use iput because that will decrease the reference count (which we only want to do in munmap)
  
  // map it
  if (mappages(pagetable, pageva, PGSIZE, (uint64)mem, PTE_U | vma->prot) != 0) {
    printf("failed to map pages\n");
    kfree(mem);
  }
  printd("mmapfaulthandler: mapped page %lx\n", pageva);
  return 0;
}

// searches through all the vma's. If the given address is in the vma range,
// it returns that vma, otherwise, it returns 0.
struct vma_t*
get_proc_vma_from_addr(uint64 va)
{
  struct vma_t* vma;
  for (int i = 0; i < NVMAS; i++) {
    vma = myproc()->proc_vmas[i];
    if (vma == 0) continue;
    if (vma_includes(vma, va)) {
      return vma;
    }
  }
  return (struct vma_t*)0;
}

// searches through all the vma's. If the given address is in the vma range,
// it returns that vma's file inode, otherwise, it returns 0.
// It does call ilock on the inode.
struct inode*
get_inode_from_proc_vmas(uint64 va)
{
  struct inode *ip;
  struct vma_t* vma;
  if ((vma = get_proc_vma_from_addr(va)) == 0) {
    return (struct inode*)0;
  }
  ip = vma->file->ip;
  ilock(ip);
  return ip;
}