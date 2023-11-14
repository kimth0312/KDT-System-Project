#include <stdio.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>

static int toy_timer = 0;

int posix_sleep_ms(unsigned int timeout_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / MILLISEC_PER_SECOND;
    sleep_time.tv_nsec = (timeout_ms % MILLISEC_PER_SECOND) * (NANOSEC_PER_USEC * USEC_PER_MILLISEC);

    return nanosleep(&sleep_time, NULL);
}

void timer_handler(int sig)
{
    toy_timer++;
    // debugging purpose
    printf("toy_timer : %d\n", toy_timer);
}

void set_timer()
{
    struct itimerval itv;

    itv.it_value.tv_sec = 5;
    itv.it_value.tv_usec = 0;
    itv.it_interval.tv_sec = 5;
    itv.it_interval.tv_usec = 0;

    if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
    {
        perror("itimer error");
        exit(-1);
    }
}

int system_server()
{
    struct itimerspec ts;
    struct sigaction sa;
    struct sigevent sev;
    timer_t *tidlist;

    printf("나 system_server 프로세스!\n");

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = timer_handler;
    if (sigaction(SIGALRM, &sa, NULL) == -1)
    {
        perror("sigaction: Timer");
        exit(-1);
    }

    set_timer();

    while (1)
    {
        sleep(1);
    }

    return 0;
}

int create_system_server()
{
    pid_t systemPid;
    const char *name = "system_server";

    printf("여기서 시스템 프로세스를 생성합니다.\n");

    /* fork 를 이용하세요 */
    if ((systemPid = fork()) < 0)
    {
        perror("system server fork error");
        exit(-1);
    }
    else if (systemPid == 0)
    {
        if (prctl(PR_SET_NAME, (unsigned long)name) < 0)
            perror("prctl()");
        system_server();
    }

    return 0;
}
