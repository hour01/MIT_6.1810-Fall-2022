## swtch (timer interrupt)
This compiler function needs two arguments, context *old and *new. It will save the old context register and load new register from new context. The other 18 registers will be saved by the c compiler(RISC-V) when the call happens.
The most important register is new context register ra, which determines where to return. 
 
In the timer interrupt case, given interrupted thread a, thread a will trap into its kernel thread first. When it calls sched, swtch in sched will switch this kernel thread to scheduler, which comes from cpu->context->ra and belongs to each cpu, it will be set when booting.
 
When the thread is chosen by the scheduler at some time, we will see that the scheduler will switch to the p->context->ra to continue to the breakpoint in thread a. If there is another timer interrupt or anything else that thread calls yield to give up the cpu, the switch from kernel thread to scheduler will finally reach the next line of swtch() in scheduler which is the cpu->context->ra be saved when thread a is set to RUNNING. So the cpu->context->ra will mostly be saved as the next line of the `swtch(&c->context, &p->context);`. 

You have to acquire the `p->lock` before calling the sched.The `release(&p->lock); ` after `c->proc = 0;` in scheduler release the lock acquired in yield. The reason why it has to be done in scheduler(after swtch) but not before the sched in yield is that its context has to be stored when one of other cpus scheduler choose it to run.  

## lost wake up 
In short, lost wake-up is the thing that one process does not sleep yet, but it is woken up by the other process. 

When it comes to multithread, there is one case in which some process wants to give up the cpu personally because it is waiting for I/O or something else which has a lower speed. Generally, there is a flag that represents the data that the process need is ready or not and we need a flag lock to protect this flag to avoid racing. So, that is the story. 

```
f(){
    acquire(lock_flag);
    while(flag==0){
        release(lock_flag);
        sleep(p);
        acquire(lock_flag);
    }
    release(lock_flag);
}

g(){
    if(xxxx){
        acquire(lock_flag);
        flag == 1;
        wakeup(p);
        release(lock_flag);
    }
}
```

This will definitely protect the flag, but there is an issue that what would happen if the wakeup occurs before the `sleep(p)`. The answer is it will miss this wake-up call, but we have to release the lock before sleeping, it seems there is a discrepancy.  
In xv6, this will be solved by appending one parameter to the sleep function which is the lock. and utilize p->lock both in sleep and wake-up function to make sure the lost-wakeup wouldn't happen.  