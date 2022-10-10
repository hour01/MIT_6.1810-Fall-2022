#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

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


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  int num;
  unsigned int bitmask = 0;
  uint64 va, bitmask_addr;
  argaddr(0, &va);
  argint(1, &num);
  argaddr(2, &bitmask_addr);

  struct proc *p = myproc();
  printf("%p\n",va+PGSIZE);
  vmprint(p->pagetable);
  pte_t *pte;
  for(int i=0;i<num;i++)
  {
    pte = walk(p->pagetable, va, 0);
    if(pte == 0)
      return -1;
    /* if pte has been accessed add bit of bitmask and clear*/
    if(*pte & PTE_A)
    {
      bitmask |= (1 << i);
      *pte &= (~PTE_A);
    }
    /* va of next page */
    va += PGSIZE;  
  }

  if(copyout(p->pagetable,bitmask_addr,(char *)&bitmask,sizeof (bitmask)) < 0)
    return -1;
  return 0;
}
#endif

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
