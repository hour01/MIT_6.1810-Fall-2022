#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"
// struct file {
//   enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
//   int ref; // reference count
//   char readable;
//   char writable;
//   struct pipe *pipe; // FD_PIPE
//   struct inode *ip;  // FD_INODE and FD_DEVICE
//   uint off;          // FD_INODE
//   short major;       // FD_DEVICE
// };

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// return the idx of the vmas, -1 for no valid vma.
int alloc_vma(void)
{
  struct proc *p = myproc();
  for(int i=0;i<MAXVMA;i++)
    if(!(p->vmas[i].valid))
      return i;
  return -1;
}

uint64 sys_mmap(void)
{
  struct file *f; 
  int length , prot, flags, offset; 
  uint64 addr; 
  // get args, 0:addr, 1:length, 2:prot, 3:flags, 4:fd=>*f, 5:offset
  argaddr(0, &addr);
  argint(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  if(argfd(4, 0, &f) < 0)
    return -1;
  argint(5, &offset);
  uint64 mmap_error = 0xffffffffffffffff;
  if(!file_writable(f) && (prot & PROT_WRITE) && (flags == MAP_SHARED))
    return mmap_error;

  struct proc *p = myproc();

  int idx = alloc_vma();
  if(idx == -1)
    return mmap_error;
  
  p->vmas[idx].f = f;
  p->vmas[idx].length = length;
  p->vmas[idx].prot = prot;
  p->vmas[idx].flags = flags;
  p->vmas[idx].valid = 1;
  p->vmas[idx].addr = p->sz;
  p->sz += p->vmas[idx].length;
  filedup(f);

  return p->vmas[idx].addr;
}

uint64 sys_munmap(void)
{
  uint64 addr;
  int length;
  argaddr(0, &addr);
  argint(1, &length);
  struct proc *p = myproc();

  // find the vma
  int idx;
  for(idx=0;idx<MAXVMA;idx++)
    if(p->vmas[idx].valid && p->vmas[idx].addr <= addr 
          && addr < (p->vmas[idx].addr+p->vmas[idx].length))
      break;
  if(idx == MAXVMA)
    return -1;

  for(int addr_tmp = addr;addr_tmp<addr+length;addr_tmp+=PGSIZE)
  {
    if(walkaddr(p->pagetable, addr_tmp) != 0)
    {
      // write back to file
      if((p->vmas[idx].flags == MAP_SHARED && (p->vmas[idx].prot & PROT_WRITE)) 
          && filewrite(p->vmas[idx].f, addr_tmp, PGSIZE) == -1)
        return -1;
      uvmunmap(p->pagetable,addr_tmp,1,1);
    }
  }
  
  if(addr == p->vmas[idx].addr)
    p->vmas[idx].addr += length;
  p->vmas[idx].length -= length;
  
  if(p->vmas[idx].length == 0)
  {
    fileclose(p->vmas[idx].f);
    p->vmas[idx].valid = 0;
  }
    
  return 0;
}