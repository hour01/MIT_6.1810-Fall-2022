#include "kernel/types.h"
#include "user.h"

void f(int *p)
{
    int prime_num, tmp_num;
    // no more numbers, end of the recursion
    if(0 == read(p[0],&prime_num,4))
    {    
        close(p[0]);
        exit(0);
    }    

    printf("prime %d\n",prime_num);

    int p_new[2];
    pipe(p_new);
    if(fork() == 0)
    {
        close(p_new[1]);
        close(p[0]);
        f(p_new);
    }
    else
    {
        close(p_new[0]);
        while(read(p[0],&tmp_num,4) != 0)
            if(tmp_num % prime_num)
                write(p_new[1],&tmp_num,4);
        close(p[0]);
        close(p_new[1]);
        wait(0);
    }
    exit(0);
    
}

int main()
{
    int p[2];
    pipe(p);
    if(fork() == 0)
    {
        close(p[1]);
        f(p);
    }
    else
    {
        close(p[0]);
        for(int i=2;i<=35;i++)
            write(p[1],&i,4);
        close(p[1]);
        wait(0);
    }
    exit(0);
}