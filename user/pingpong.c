#include "kernel/types.h"
#include "user.h"

int main(int argc,char* argv[])
{
    int p1[2];
    int p2[2];
    pipe(p1);
    pipe(p2);
    // father
    if(fork() > 0)
    {
        char buf[5];
        close(p1[0]);
        close(p2[1]);
        write(p1[1],"ping",4);
        read(p2[0],buf,4);
        printf("%d: received pong\n",getpid());
        wait(0);
        exit(0);
    }
    else
    {
        char buf[5];
        close(p1[1]);
        close(p2[0]);
        read(p1[0],buf,4);
        printf("%d: received ping\n",getpid());
        write(p2[1],"pong",4);
        exit(0);
    }
}