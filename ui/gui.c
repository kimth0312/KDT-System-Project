#include <stdio.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <unistd.h>

int create_gui()
{
    pid_t systemPid;

    printf("여기서 GUI 프로세스를 생성합니다.\n");

    sleep(3);

    if ((systemPid = fork()) < 0)
    {
        perror("GUI fork error");
        exit(-1);
    }
    else if (systemPid == 0)
    {
        execl("/usr/bin/google-chrome-stable", "google-chrome-stable", "http://localhost:8282", NULL);
    }

    return 0;
}
