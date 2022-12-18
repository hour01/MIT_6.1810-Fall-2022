# lab traps

## RISC-V assembly
answers-traps.txt

## Backtrace
### 内容分析 
实现在错误发生时可以打印出当前调用栈，有助于debug

### 设计方法
- 因为栈帧指针被保存在s0寄存器，故首先在`kernel/riscv.h`中添加一个获取s0寄存器的函数：
```C
static inline uint64
r_fp()
{
  uint64 x;
  asm volatile("mv %0, s0" : "=r" (x) );
  return x;
}
```
- 由于栈是由高地址向低地址增长的，因此使用PGROUNDUP获得栈底地址，之后循环打印栈帧的函数的返回地址。
> Note that the return address lives at a fixed offset (-8) from the frame pointer of a stackframe, and that the saved frame pointer lives at fixed offset (-16) from the frame pointer.
```C
void backtrace(void)
{
  uint64 fp = r_fp();
  printf("backtrace:\n");
  // printf("%p, %p, %p\n",PGROUNDDOWN(fp), PGROUNDUP(fp), fp);
  
  // previous fp lives at fixed offset(-16),
  // the return address lives at a fixed offset (-8).
  // the return address is the address of the next instruction 
  // after returnning from this procedure.
  for(uint64 r = fp; r < PGROUNDUP(fp); r = *((uint64*)(r - 16)))
    printf("%p\n",*(uint64*)(r-8));
}
```


## Alarm 
### 内容分析
- 该部分需要添加系统调用`sigalarm`来实现当用户程序运行了n个ticks后，触发一次回调函数。同时，时钟中断的处理是在`usertrap`函数中的if(which_dev == 2)里面的。

### 设计方法
- 首先在PCB中添加相应字段，除了设置触发条件、计数器、回调函数外，增加一个`timer_trapframe`，用来储存进入回调函数前的trapframe。
```C
// Per-process state
struct proc {
  int alarm_ticks;             // alarm every n ticks
  int past_alarm_ticks;        // ticks have passed since the last alarm
  uint64 alarm_handler;        // handler for alarm
  struct trapframe *timer_trapframe; // saves registers to resume in sigret 
  int handler_execute;         // handler executing  => 1, handler no executing => 0
};
```

- 实现`sys_alarm`，放在`sysproc.c`，将传入系统调用的参数信息（ticks数、处理函数地址）保存到PCB。
```C
uint64 sys_sigalarm(void)
{
  int ticks;
  uint64 handler;
  argint(0, &ticks);
  argaddr(1, &handler);

  struct proc* p = myproc();
  p->alarm_ticks = ticks;
  p->alarm_handler = handler;

  return 0;
}
```

- 关键部分是在usertrap中，当发生时钟中断时(which_dev == 2)，将`p->past_alarm_ticks`增加，如果p->past_alarm_ticks == p->alarm_ticks，那么就要触发一次回调函数，而触发的方法就是将p->trapframe->epc设置为回调函数地址，当陷阱处理程序结束后就会跳转到回调函数；而为了保证回调函数不会破坏原程序的寄存器，需要对trapframe进行保存；然后将trapframe复制一份进PCB对应字段；为了保证回调函数执行期间不会重复调用，就可以判断p->handler_execute是否为0，不为0说明上一次的回调函数还没有调用sigreturn，即函数未结束。
- 同时在usertrap中，为了能使进程调用`sys_sigreturn`陷入内核态后能恢复原始寄存器，在usertrap中，sysycall()调用后，判断如果这次syscall是调用`sys_sigreturn`，则将保存在PCB中的`timer_trapframe`恢复到trapframe中。
```C
...

if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
    if(p->trapframe->a7 == SYS_sigreturn)
    {
      p->trapframe->a0 = p->timer_trapframe->a0;
      memmove(p->trapframe, p->timer_trapframe, sizeof(struct trapframe));
      p->handler_execute = 0;
    }
  }
....
if(which_dev == 2)
    {
      if(p->alarm_ticks != 0)
      {
        p->past_alarm_ticks ++;
        if(p->past_alarm_ticks == p->alarm_ticks && p->handler_execute == 0)
        {
          p->past_alarm_ticks = 0;
          memmove(p->timer_trapframe, p->trapframe , sizeof(struct trapframe));
          p->handler_execute = 1;
          p->trapframe->epc = p->alarm_handler;
        }
      }
    }
...
```

- 最后就是sigreturn函数，而基于上述设计，该系统调用什么都不用做。同时在freeproc函数中也要对p->alarm_trapframe进行判断，防止程序异常结束时该页面没有被释放。
```C
uint64 sys_sigreturn(void)
{
  return 0;
}
```

## 对于`timer_trapframe`保存恢复的原因和实现逻辑：
规定 :用户重定义了handler后，需要在函数退出时调用sigreturn系统调用

当进程因为`timer interrupt`从用户态陷入内核态后，且该次陷入达到累计次数`alarm_ticks`，将此时用户态的断点称为B;
则此时该进程从第一次陷入中返回到用户态，但因为在`usertrap`中修改了`trapframe->epc = 用户自定义的handler的地址`，故此时返回到用户态后执行handler代码
当handler调用`sigreturn`后，进程第二次陷入内核态，此时执行系统调用`sigreturn`，再次准备返回用户态是，此时的trapframe并不是断点B时的trapframe，而是handler中的trapframe，故程序无法返回到B。
例子：
```
handler()
{
1    ...
2    ...
3    sigreturn()
}

main(){
4  ...
5  ...
6  ...
陷入timer interrupt
7  ...
}

```
上述代码在第6条语句后陷入timer interrupt，若如上所述仅仅在usertrap中简单判断一下，**第二次陷入内核后返回用户态的位置是第3条语句后**
而我们原本认为的是返回到第7条语句。
所以需要在proc中申请一个trapframe副本，在第一次需要调用handler时，在usertrap内保存trapframe。
并且因为sigreturn是一个系统调用，返回值会保存在p->trapframe->a0，所以恢复所有寄存器需要放在usertrap()中syscall()调用后。