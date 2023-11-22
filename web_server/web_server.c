#define _GNU_SOURCE
#include <linux/sched.h>
#include <sched.h>
#include <stdio.h>
#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <sys/wait.h>

#define STACK_SIZE (1024 * 1024)

void childFunc()
{
    execl("/usr/local/bin/filebrowser", "filebrowser", "-p", "8282", (char *)NULL);
}

int create_web_server()
{
    pid_t systemPid;

    printf("여기서 Web Server 프로세스를 생성합니다.\n");

    char *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED)
        perror("mmap");

    systemPid = clone(childFunc, stack + STACK_SIZE, CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, NULL);
    if (systemPid < 0)
    {
        perror("web server fork error");
        exit(-1);
    }

    munmap(stack, STACK_SIZE);

    return 0;
}
