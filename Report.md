# Lab: Multithreading 
 
## Uthread: switching between threads 
### 内容分析 
实现用户态线程切换，切换思想类似内核态下的进程切换，但只需要考虑寄存器的切换，不需要考虑trapframe的切换问题。 
故整体逻辑结构和内核态下的进程切换类似： 

- 实现一个能够保存和恢复当前运行状态的机制，利用其完成线程切换。 

### 设计方法 
- 增加一个用汇编语言编写的上下文切换函数thread_switch(struct context* old, struct context* new)，其代码和内核中的swtch完全一样，保存当前运行环境下的`callee register`到old，将当前运行环境下的callee寄存器更新为new中对应的值。 
> `Swtch(kernel/swtch.S:3)`saves only callee-saved registers; the C compiler generates code in the caller to save caller-saved registers on the stack. 
- 在线程控制结构体中增加context字段，用于保存`callee register` 
- 在thread_init中，当确定发生切换后(`current_thread != next_thread`)，调用上述定义的thread_switch函数完成线程切换。 
- 在thread_create中，线程创建时，需要将其context中的ra寄存器设为传入函数func的地址，这样当该线程第一次切换为`RUNNING`状态时，就会从thread_switch跳转到func函数所在地址，同时将栈顶地址保存到sp寄存器（从大到小）。 

## Using threads 
### 内容分析 
利用锁实现多线程并行，实验中需要利用多线程“同时”将若干(key,value)写入全局唯一table中，观察速度的提升。 
但因为并行情况下多个线程同时访问同一个全局变量会导致写入错误，故需要用锁进行同步。 
同时总体的table根据哈希映射为5个子table，可以使用更细粒度的锁来对单个table进行控制，获得更快的访问速度。  


### 设计方法 
- 只需要在访问table前后获取、释放锁，保证全局变量访问的唯一性。 


## Barrier 
### 内容分析 
创建若干个线程，每个线程重复进行for循环，但要求所有线程同步进行，即当还有线程正在第i轮时，要确保没有线程能够进入第i+1轮for循环，即率先进入设定的阻隔区的线程需要进入睡眠。 
设置全局变量nthread和round，前者记录当前round中已经被阻隔（睡眠）的线程，每阻隔一个线程就使nthread自增1，直到nthread等于建立的总线程数时，由最后一个被阻隔的线程更新nthread为0，round++，再将被阻隔的线程唤醒，故需要一个睡眠锁和一个同步锁。 

### 设计方法 
- 根据需求，显然需要对全局变量nthread，round加锁，使用barrier_mutex来维护 
- 为了防止lost wake up 的发生，睡眠锁的设计需要传入barrier_mutex，保证在睡眠前释放该锁，在唤醒后重新获得该锁 
- 故barrier函数中，所有线程都做的动作就是获取同步锁，更新`barrier.nthread++`，检查当前`barrier.nthread++==nthread`是否成立，即检查当前进入的线程是否是最后一个进入的线程，如果不是则进入睡眠，否则作为最后一个进入阻隔的线程，需要将barrier.nthread置0，round增1，唤醒其他线程；最后释放同步锁。