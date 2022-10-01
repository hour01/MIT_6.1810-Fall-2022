#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char* dir, char* file_name)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(dir, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", dir);
        return;
    }
    if(fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", dir);
        close(fd);
        return;
    }
    if(st.type == T_DIR)
    {
        if(strlen(dir) + 1 + DIRSIZ + 1 > sizeof buf)
        {
            printf("find: path too long\n");
            return;
        }
        strcpy(buf, dir);
        p = buf+strlen(buf);  //point to end
        *p++ = '/';
        // all files in this DIR
        while(read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if(de.inum == 0)
                continue;
            // over-write last loop's name
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            //buf is 'path/name'
            if(stat(buf, &st) < 0)
            { 
                printf("find: cannot stat %s\n", buf);
                continue;
            }
            if(st.type == T_FILE && strcmp(de.name,file_name)==0)
                printf("%s\n",buf);
            else if(st.type == T_DIR && strcmp(de.name,".")!=0 && strcmp(de.name,"..")!=0)
                find(buf, file_name);
        }
    }
    else
        fprintf(2, "find: the second argument is not a directory\n");
    close(fd);
}


int main(int argc, char *argv[])
{
    if(argc != 3)
    {
        printf("find: 3 arguments required\n");
    }

    find(argv[1],argv[2]);
    exit(0);
}