#include <stdio.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <mqueue.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <camera_HAL.h>
#include <toy_message.h>
// #include <bits/mqueue.h>

pthread_mutex_t system_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t system_loop_cond = PTHREAD_COND_INITIALIZER;
bool system_loop_exit = false;

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;

static int toy_timer = 0;

void signal_exit(void);

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
    // signal_exit();
    // debugging purpose
    // printf("toy_timer : %d\n", toy_timer);
}

void set_timer(int time)
{
    struct itimerval itv;

    itv.it_value.tv_sec = time;
    itv.it_value.tv_usec = 0;
    itv.it_interval.tv_sec = time;
    itv.it_interval.tv_usec = 0;

    if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
    {
        perror("itimer error");
        exit(-1);
    }
}

void *watchdog_thread_handler(void *arg)
{
    char *s = (char *)arg;
    int mqretcode;
    toy_msg_t msg;
    struct mq_attr attr;
    void *buffer;

    printf("%s", s);

    watchdog_queue = mq_open("/watchdog_queue", O_RDWR);
    mq_getattr(watchdog_queue, &attr);

    buffer = malloc(attr.mq_msgsize);

    printf("watchdog thread handler operating\n");
    while (1)
    {
        mqretcode = mq_receive(watchdog_queue, buffer, attr.mq_msgsize, 0);
    }
}

void *monitor_thread_handler(void *arg)
{
    char *s = (char *)arg;
    int mqretcode;
    toy_msg_t msg;
    struct mq_attr attr;
    void *buffer;

    printf("%s", s);

    monitor_queue = mq_open("/monitor_queue", O_RDWR);
    mq_getattr(monitor_queue, &attr);

    buffer = malloc(attr.mq_msgsize);

    printf("monitor thread handler operating\n");
    while (1)
    {
        mqretcode = mq_receive(monitor_queue, buffer, attr.mq_msgsize, 0);
    }
}

void *disk_service_thread_handler(void *arg)
{
    char *s = (char *)arg;
    FILE *apipe;
    char buf[1024];
    char cmd[] = "df -h ./";

    int mqretcode;
    toy_msg_t msg;
    struct mq_attr attr;
    void *buffer;

    printf("%s", s);

    disk_queue = mq_open("/disk_queue", O_RDWR);
    mq_getattr(disk_queue, &attr);

    buffer = malloc(attr.mq_msgsize);

    while (1)
    {
        mqretcode = mq_receive(disk_queue, buffer, attr.mq_msgsize, 0);
    }

    while (1)
    {
        apipe = popen(cmd, "r");
        if (apipe == NULL)
        {
            printf("popen() failed\n");
            continue;
        }

        while (fgets(buf, 1024, apipe) != NULL)
            printf("%s", buf);

        pclose(apipe);
        sleep(5);
    }
}

void *camera_service_thread_handler(void *arg)
{
    char *s = (char *)arg;
    int mqretcode;
    toy_msg_t msg;

    printf("%s", s);

    camera_queue = mq_open("/camera_queue", O_RDWR);

    while (1)
    {
        mqretcode = mq_receive(camera_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("camera_service_thread : You've got a message\n");
        printf("msg.type : %d\n", msg.msg_type);
        printf("msg.param1 : %d\n", msg.param1);
        printf("msg.param2 : %d\n", msg.param2);

        toy_camera_open();
        toy_camera_take_picture();
    }

    return 0;
}

void signal_exit(void)
{
    pthread_mutex_lock(&system_loop_mutex);
    printf("No more looping.. Bye!\n");
    system_loop_exit = true;
    pthread_cond_signal(&system_loop_cond);
    pthread_mutex_unlock(&system_loop_mutex);
}

int system_server()
{
    struct itimerspec ts;
    struct sigaction sa;
    struct sigevent sev;
    timer_t *tidlist;
    pthread_t watchdog_thread_tid, monitor_thread_tid, disk_service_thread_tid, camera_service_thread_tid;

    printf("나 system_server 프로세스!\n");

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = timer_handler;
    if (sigaction(SIGALRM, &sa, NULL) == -1)
    {
        perror("sigaction: Timer");
        exit(-1);
    }

    // camera_queue = mq_open("/camera_queue", O_RDWR, 0666, NULL);
    set_timer(10);

    pthread_create(&watchdog_thread_tid, NULL, watchdog_thread_handler, NULL);
    pthread_create(&monitor_thread_tid, NULL, monitor_thread_handler, NULL);
    pthread_create(&disk_service_thread_tid, NULL, disk_service_thread_handler, NULL);
    pthread_create(&camera_service_thread_tid, NULL, camera_service_thread_handler, NULL);

    pthread_detach(watchdog_thread_tid);
    pthread_detach(monitor_thread_tid);
    pthread_detach(disk_service_thread_tid);
    pthread_detach(camera_service_thread_tid);

    printf("system init done. waiting...\n");

    pthread_mutex_lock(&system_loop_mutex);
    while (system_loop_exit == false)
    {
        pthread_cond_wait(&system_loop_cond, &system_loop_mutex);
    }
    pthread_mutex_unlock(&system_loop_mutex);

    printf("<== system\n");
    while (system_loop_exit == false)
    {
        sleep(1);
    }

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
