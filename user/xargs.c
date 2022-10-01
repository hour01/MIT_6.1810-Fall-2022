#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"
#define MAXARGLEN 100


int main(int argc,char* argv[])
{
    char* args[MAXARG];
    int idx = 0, p = 0;
    char tmp;
    for(int i=1;i<argc;i++)
        args[idx++] = argv[i];
    args[idx] = (char*)malloc(sizeof(char)*MAXARGLEN);
    int top = idx;

    while(read(0, &tmp, sizeof(char)))
    {
        if(tmp == '\n')
        {
            *(args[idx++] + p) = 0;
            if(idx > top)
                args[idx] = (char*)malloc(sizeof(char)*MAXARGLEN),top = idx;
            *(args[idx]) = 0;
            if(fork() == 0)
            {
                exec(argv[1],args);
                exit(0);
            }
            wait(0);
            idx = argc-1,p = 0;
        }
        else if(tmp == ' ')
        {
            *(args[idx++] + p) = 0;
            p = 0;
            if(idx > top)
                args[idx] = (char*)malloc(sizeof(char)*MAXARGLEN),top = idx;
        }
        else
            *(args[idx] + p++) = tmp;
            
    }
    for(int i=argc-1;i<=top;i++)
        free(args[i]);
    exit(0);
}