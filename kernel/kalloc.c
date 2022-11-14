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

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
} ;

// for each cpu
struct kmem kmems[NCPU];
// to avoid dead lock from more than one cpu want to steal
struct spinlock steal_lock;

void
kinit()
{
  for(int i=0;i<NCPU;i++)
    initlock(&kmems[i].lock, "kmem");
  initlock(&steal_lock, "kmem_steal");
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

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int cpu_id = cpuid();
  

  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;

  
  release(&kmems[cpu_id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cpu_id = cpuid();
  
  acquire(&kmems[cpu_id].lock);
  r = kmems[cpu_id].freelist;

  if(r)
    kmems[cpu_id].freelist = r->next;
  // steal from other cpu
  else
  {
    // avoid racing condition
    acquire(&steal_lock);

    for(int cpu = 0;cpu<NCPU;cpu++)
    {
      if(cpu == cpu_id)
        continue;
      acquire(&kmems[cpu].lock);
      if(kmems[cpu].freelist)
      {
        r = kmems[cpu].freelist;
        kmems[cpu].freelist = r->next;
        release(&kmems[cpu].lock);
        break;
      }
      release(&kmems[cpu].lock);
    }

    release(&steal_lock);
  }

  release(&kmems[cpu_id].lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
