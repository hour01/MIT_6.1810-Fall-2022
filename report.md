# Lab: Copy-on-Write Fork for xv6

## 内容分析
实现`Copy-on-Write Fork`，即进程fork后，暂时不为子进程分配内存空间，直接使用父进程的映射，但将所有pte设置为只读；当子进程写入时引发`page fault`，这时内核才为其分配对应的内存空间，同时pte标志为读写。

## 设计方法 
- 首先定义好`kalloc.c中的kmem`中refer_count的定义，目的是记录每一个页起始物理地址的引用次数；因为cow涉及到多个进程会共享同一物理内存，而在最后一个进程推出前，该物理页都不希望被释放；`increase_refer`对其增一，`get_refcount`获取对应物理地址的引用，在`kalloc,kinit`中对新分配的置1，在`kfree`中将对应物理地址引用-1。
```C
struct {
  struct spinlock lock;
  struct run *freelist;
  // each page's reference count, 
  // the index of array is the page's physical address divided by PGSIZE
  uint8    refer_count[(PHYSTOP - KERNBASE) / PGSIZE];
} kmem;

// increment reference counts on pa.
void increase_refer(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("increase_refer");

  acquire(&kmem.lock);
  kmem.refer_count[((uint64)pa - KERNBASE) / PGSIZE]++;
  release(&kmem.lock);
}

int get_refcount(void* pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("get_refcount");
  int res;
  acquire(&kmem.lock);
  res = kmem.refer_count[((uint64)pa - KERNBASE) / PGSIZE];
  release(&kmem.lock);
  return res;
}

void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    kmem.refer_count[((uint64)r - KERNBASE) / PGSIZE] = 1;
  }  
  release(&kmem.lock);
  ...
}

void
kfree(void *pa)
{
  ...
  // should only place a page back on the free list,
  // when its reference count is zero. 
  acquire(&kmem.lock); 
  if((-- kmem.refer_count[((uint64)pa - KERNBASE) / PGSIZE]) > 0){
    release(&kmem.lock);
    return;
  }
  release(&kmem.lock);
  ...
}
```
- 修改uvmcopy()，使得在子进程页表直接将当前所有虚拟地址映射到原父进程对应的物理地址中；映射前若原pte被设置为写，则对子进程取消该写权限，同时增加`PTE_RSW_COW`的标记，不进行判断会导致原来只读的部分在cow中变为可写；调用increase_refer进行增一。其中`PTE_RSW_COW`是新引入的pte标记，根据`riscv-privileged，P77`，可以将该标记位定义在扩展位上。
```C
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
    ...

    for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    // set unwritable and cow
    // must have this if(), PTE_W is basic reauirement
    if(*pte & PTE_W){
      *pte &= ~PTE_W;
      *pte |= PTE_RSW_COW;
    }
    flags = PTE_FLAGS(*pte);

    if(mappages(new, i, PGSIZE, (uint64)pa, flags) != 0){
      goto err;
    }
    increase_refer((void *)pa);
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

- 修改`usertrap()`，可以通过`r_stval()`获取发生页错误的虚拟地址fault_va，`is_cowpage`对fault_va对应的pte是否带有`PTE_RSW_COW`进行了判断，`cow_alloc`对fault_va所在的页进行了分配，其中如果pa对应的引用为1，说明该物理地址只有该进程在使用，可以不用进行内存分配，直接修改pte的flag即可，否则就申请内存，提取flag，做好映射即可，同时在调用`mappages`前要清空`PTE_V`，防止remap。
```C
void
usertrap(void)
{
    ...

    else if(r_scause() == 15){
    // store page fault for cowfork
    uint64 fault_va = r_stval();
    // pte_t* fault_pte = walk(p->pagetable, fault_va, 0);
    // make sure it's cow_page
    // must have PGROUNDDOWN
    if((fault_va > p->sz) || !is_cowpage(p->pagetable,fault_va) || (cow_alloc(p->pagetable, PGROUNDDOWN(fault_va)) == 0)){
      // ordinary page fault
      printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      setkilled(p);
    }
  }

  ...
}

void* cow_alloc(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  uint64 pa = PTE2PA(*pte);

  // refcount == 1, only a process use the cowpage
  // so we set the PTE_W of cowpage and clear PTE_COW of the cowpage
  if(get_refcount((void*)pa) == 1){
    *pte |= PTE_W;
    *pte &= ~PTE_RSW_COW;
    return (void*)pa;
  }

  // refcount >= 2, some processes use the cowpage
  uint flags;
  char *new_mem;
  /* sets PTE_W */
  *pte |= PTE_W;
  flags = PTE_FLAGS(*pte);
  
  /* alloc and copy, then map */
  pa = PTE2PA(*pte);
  // If a COW page fault occurs and there's no free memory, the process should be killed.
  if((new_mem = kalloc())==0){
    return 0;
  }

  memmove(new_mem, (char*)pa, PGSIZE);
  /* clear PTE_V before map the page to avoid panic of 'remap'  */
  *pte &= ~PTE_V;
  /* note: new_mem is new address of phycial memory*/
  if(mappages(pagetable, va, PGSIZE, (uint64)new_mem, flags) != 0){
    /* set PTE_V, then kfree new_men, if map failed*/
    *pte |= PTE_V;
    kfree(new_mem);
    return 0;
  }

  /* decrement a ref_count */
  kfree((char*)pa);

  return new_mem;
}
```

- 最后在`copyout`中，如果遇到是cowpage，则需要做和usertrap中同样的事，因为copyout是在内核中调用的，缺页错误不会进入usertrap。
```C
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
    ...
    if(is_cowpage(pagetable, va0))
      // if it is a cowpage, we need a new pa0 pointer to a new memory
      // and if it is a null pointer, we need return error of -1
      if ((pa0 = (uint64)cow_alloc(pagetable, va0)) == 0)
        return -1;
    ...
}
```