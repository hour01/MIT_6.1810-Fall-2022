# Lab: mmap 

## 内容分析 
- 本次实验要实现`mmap,munmap`两个系统调用，前者的作用是可以在内存中申请一块区域，将文件内容读取到这部分区域，从而加速进程对频繁读取的文件的读取速度；同时若传入标记为`MAP_SHARED`，则在调用munmap时会将对应文件的在内存中的内容写回到磁盘文件中。 
- 同时本次实验采用`lazy allocation`的分配规则，mmap中仅对PCB增加相关信息而不实际分配内存。 

## 设计方法 
- 首先，进程需要对mmap的信息做记录，为后续缺页时进行内存分配、读取文件，调用munmap取消映射时提供信息；故定义如下数据结构，同时将该vma的数组放置在PCB中(根据实验指导书定义大小为16的固定数组)：
```C
// virtual memory area
struct vma {
  uint64 addr;      // begin of the file
  int length;       // file length
  int prot;         // permisions
  int flags;        //MAP_SHARED or MAP_PRIVATE
  struct file *f;   //the file be mapped
  int valid;
};
```
- 实现`mmap(sys_proc.c)`，核心步骤是将信息写入到PCB中的vma，如下所示；其中alloc_vma即对PCB中的vma数组进行遍历，找到valid为0的下标，然后写入信息，同时使用`filedup`增加该文件的引用：
```C
uint64 sys_mmap(void)
{
  ...

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
```

- 在`trap.c`中增加相关缺页处理，进行`lazy allocation`。核心是增加一个`mmap_lazyalloc`函数，通过缺页的地址，找到对应的vma，每页错误为其分配一页内存，从vma中获取权限信息，在页表中对分配的内存增加映射，调用`read_inode`将文件中的内容写入该内存区域（`read_inode`是在`fs.c`中增加的对`readi`调用的外部接口），其中文件内部的偏移可以通过`va-vma.addr`获得；后将该函数插入usertrap中恰当位置，`r_scause() == 13 || r_scause() == 15`代表page fault。
```C
int mmap_lazyalloc(uint64 va)
{
  struct proc *p = myproc();

  // find the specific vma
  int idx;
  for(idx=0;idx<MAXVMA;idx++)
    if(p->vmas[idx].valid && p->vmas[idx].addr <= va 
          && va < (p->vmas[idx].addr+p->vmas[idx].length))
      break;
  if(idx == MAXVMA)
    return -1;

  // get the permission
  struct file *f = p->vmas[idx].f;
  int prot = p->vmas[idx].prot;
  int perm = PTE_U;
  if(prot & PROT_READ)
    perm |= PTE_R;
  if(prot & PROT_WRITE)
    perm |= PTE_W;
  if(prot & PROT_EXEC)
    perm |= PTE_X;

  if(va >= myproc()->sz || va <= PGROUNDDOWN(myproc()->trapframe->sp))
    return -1;

  char *mem;
  mem = kalloc();
  if(mem == 0)
    return -1;
  memset(mem, 0, PGSIZE);

  // map
  if(mappages(myproc()->pagetable, va, PGSIZE, (uint64)mem, perm) != 0){
    kfree(mem);
    uvmdealloc(myproc()->pagetable, va+PGSIZE, va);
    return -1;
  }

  // read from file, write to memory
  if(read_inode(f, 1, va, va - p->vmas[idx].addr, PGSIZE) != 0)
    return -1;
  return 0;
}

usertrap()
{
    ...
    else if(r_scause() == 13 || r_scause() == 15){
    // page fault, lazy alloction
    if (mmap_lazyalloc(r_stval())==-1)
    { printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      setkilled(p);
    }
    ...
}
```

- 实现`munmap`，释放内存中文件内容部分的内存，取消页表映射；首先通过传入的地址在PCB中找到对应的vma；因为采用了`lazy allocation`的策略，故在`munmap`时存在记录中有些地址段还没有进行分配内存的情况，故在调用`uvmunmap,filewrite`前先通过`walkaddr`检查释放的页是否已经分配过；如果确定pte存在，除了进行`uvmunmap`外，对于`flags为MAP_SHARED`且`在mmap时被标记为PROT_WRITE`的，还需要将内存中的内容写入文件中（使用filewrite）。
```C
uint64 sys_munmap(void)
{
  ...
  // check addr_tmp is mapped or not
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
```

- 在`fork()`和`exit()`增加相关代码段，实现子进程可以复制父进程的vma；在`exit()`时与munmap类似，将即将exit()的进程中关于mmap部分全部取消映射。同时需要将`uvmcopy`中找不到pte的情况修改为`pass`而不是`panic`，防止在fork时某些地址还没有分配内存；还需要将`uvmunmap`中找不到pte的情况修改为`pass`而不是`panic`，防止在exit时某些地址还没有分配内存。
```C
int
fork(void)
{
    ...
    // mmap
  for(int i=0;i<MAXVMA;i++)
  {
    if(p->vmas[i].valid)
    {
      memmove(&(np->vmas[i]), &(p->vmas[i]), sizeof(p->vmas[i]));
      filedup(p->vmas[i].f);
    }
  }
    ...
}

void
exit(int status)
{
  ...
  // munmap
  for(int idx=0;idx<MAXVMA;idx++)
  {
    if(p->vmas[idx].valid)
    {
      for(int addr_tmp = p->vmas[idx].addr;
          addr_tmp < p->vmas[idx].addr+p->vmas[idx].length;addr_tmp+=PGSIZE)
      {
        if(walkaddr(p->pagetable, addr_tmp) != 0)
        {
          // write back to file
          if((p->vmas[idx].flags == MAP_SHARED && (p->vmas[idx].prot & PROT_WRITE)) 
              && filewrite(p->vmas[idx].f, addr_tmp, PGSIZE) == -1)
            panic("exit-munmap");
          uvmunmap(p->pagetable,addr_tmp,1,1);
        }
      }
      fileclose(p->vmas[idx].f);
      p->vmas[idx].valid = 0;
    }
  }
  ...
}

```