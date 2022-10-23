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