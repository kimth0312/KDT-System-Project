#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <mqueue.h>
#include <sys/inotify.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/sysmacros.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <toy_message.h>
#include <shared_memory.h>
#include <hardware.h>

#define BUF_LEN 1024
#define TOY_TEST_FS "./fs"

pthread_mutex_t system_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t system_loop_cond = PTHREAD_COND_INITIALIZER;
bool system_loop_exit = false;

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;

static int toy_timer = 0;
pthread_mutex_t toy_timer_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t global_timer_sem;
static bool global_timer_stopped;

static shm_sensor_t *the_sensor_info = NULL;

void signal_exit(void);
void set_timer(int time, int interval);

int posix_sleep_ms(unsigned int timeout_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / MILLISEC_PER_SECOND;
    sleep_time.tv_nsec = (timeout_ms % MILLISEC_PER_SECOND) * (NANOSEC_PER_USEC * USEC_PER_MILLISEC);

    return nanosleep(&sleep_time, NULL);
}

static void timer_expire_signal_handler()
{
    // sem post 사용해서 timer_thread_handler에 루프가 활성화 될 수 있도록 만들기.
    // 시그널 핸들러 함수이기에 빠른 처리가 필요함.
    sem_post(&global_timer_sem);
}

static void system_timeout_handler()
{
    pthread_mutex_lock(&toy_timer_mutex);
    toy_timer++;
    // printf("toy timer: %d\n", toy_timer);
    pthread_mutex_unlock(&toy_timer_mutex);
}

void *timer_thread_handler(void *arg)
{
    signal(SIGALRM, timer_expire_signal_handler);
    set_timer(1, 1);

    while (!global_timer_stopped)
    {
        // sleep을 sem_wait 함수를 사용하여 동기화 처리
        sem_wait(&global_timer_sem);
        system_timeout_handler();
    }
    return 0;
}

void set_timer(int time, int interval)
{
    struct itimerval itv;

    itv.it_value.tv_sec = time;
    itv.it_value.tv_usec = 0;
    itv.it_interval.tv_sec = interval;
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

    printf("%s", s);

    printf("watchdog thread handler operating\n");
    while (1)
    {
        mqretcode = (int)mq_receive(watchdog_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("watchdog_thread: 메시지가 도착했습니다.\n");
        printf("msg.type: %d\n", msg.msg_type);
        printf("msg.param1: %d\n", msg.param1);
        printf("msg.param2: %d\n", msg.param2);
    }
}

#define SENSOR_DATA 1
#define DUMP_STATE 2

void dumpstate_handler(const char *fileName)
{
    char buf[30000];

    int fd = open(fileName, O_RDONLY);
    if (!fd)
    {
        perror("error while opening");
        exit(-1);
    }

    while (1)
    {
        int readBytes = read(fd, buf, sizeof(buf));
        // 내용이 있으면 개행 문자 추가
        if (readBytes > 0)
            buf[readBytes - 1] = '\n';
        // 내용이 없으면 반복문 break
        if (readBytes <= 0)
            break;
    }

    printf("============ %s begins ============\n\n", fileName);
    printf("%s\n", buf);
    printf("============ %s ends ============\n", fileName);

    close(fd);
}

void dumpstate()
{
    dumpstate_handler("/proc/version");
    dumpstate_handler("/proc/meminfo");
    dumpstate_handler("/proc/slabinfo");
    dumpstate_handler("/proc/vmstat");
    dumpstate_handler("/proc/vmallocinfo");
    dumpstate_handler("/proc/zoneinfo");
    dumpstate_handler("/proc/pagetypeinfo");
    dumpstate_handler("/proc/buddyinfo");
    dumpstate_handler("/proc/net/dev");
    dumpstate_handler("/proc/net/route");
    dumpstate_handler("/proc/net/ipv6_route");
    dumpstate_handler("/proc/interrupts");
}

void *monitor_thread_handler(void *arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;
    int shmid;

    printf("%s", s);

    while (1)
    {
        mqretcode = (int)mq_receive(monitor_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("monitor_thread: 메시지가 도착했습니다.\n");
        printf("msg.type: %d\n", msg.msg_type);
        printf("msg.param1: %d\n", msg.param1);
        printf("msg.param2: %d\n", msg.param2);
        if (msg.msg_type == SENSOR_DATA)
        {
            shmid = msg.param1;
            the_sensor_info = toy_shm_attach(shmid);
            printf("sensor temp: %d\n", the_sensor_info->temp);
            printf("sensor info: %d\n", the_sensor_info->press);
            printf("sensor humidity: %d\n", the_sensor_info->humidity);
            toy_shm_detach(the_sensor_info);
        }
        else if (msg.msg_type == DUMP_STATE)
        {
            dumpstate();
        }
        else
            perror("monitor_thread: unknown message.\n");
    }

    return 0;
}

// https://stackoverflow.com/questions/21618260/how-to-get-total-size-of-subdirectories-in-c
// 정확한 디렉터리 사이즈를 구하기 위해서는 내부 파일의 사이즈를 다 합쳐야 함.
static long total_dir_size(char *dirname)
{
    DIR *dir = opendir(dirname);
    if (dir == 0)
        return 0;

    struct dirent *dit;
    struct stat st;
    long size = 0;
    long total_size = 0;
    char filePath[1024];

    while ((dit = readdir(dir)) != NULL)
    {
        if ((strcmp(dit->d_name, ".") == 0) || (strcmp(dit->d_name, "..") == 0))
            continue;

        sprintf(filePath, "%s/%s", dirname, dit->d_name);
        if (lstat(filePath, &st) != 0)
            continue;
        size = st.st_size;

        if (S_ISDIR(st.st_mode))
        {
            long dir_size = total_dir_size(filePath) + size;
            total_size += dir_size;
        }
        else
        {
            total_size += size;
        }
    }
    return total_size;
}
/*
int total_dir_size(struct stat sb, const char *directory)
{
    DIR *dirp;
    struct dirent *dp;
    int total_size = 0;

    dirp = opendir(directory);
    if (dirp == NULL)
    {
        perror("open failed on fs");
        exit(-1);
    }

    while ((dp = readdir(dirp)) != NULL)
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;
        total_size += sb.st_size;
    }

    return total_size;
}
*/
void *disk_service_thread_handler(void *arg)
{
    char *s = arg;
    int inotifyFD, wd, j;
    char buf[BUF_LEN] __attribute__((aligned(8)));
    ssize_t numRead;
    char *p;
    struct inotify_event *event;
    struct stat sb;
    char *directory = TOY_TEST_FS;
    int total_size = 0;

    printf("%s", s);

    inotifyFD = inotify_init();
    if (inotifyFD == -1)
        perror("inotify_init");

    wd = inotify_add_watch(inotifyFD, directory, IN_CREATE);
    if (wd == -1)
        perror("inotify_add_watch");

    while (1)
    {
        numRead = read(inotifyFD, buf, BUF_LEN);
        if (numRead == 0)
            perror("read() from inotify fd returned 0!");
        if (numRead == -1)
            perror("read");

        printf("Read %ld bytes from inotify fd\n", (long)numRead);
        total_size = total_dir_size(directory);

        printf("Total directory size : %d\n", total_size);
    }

    return 0;
}

#define CAMERA_TAKE_PICTURE 1

void *camera_service_thread_handler(void *arg)
{
    char *s = (char *)arg;
    int mqretcode;
    toy_msg_t msg;
    hw_module_t *module = NULL;
    int res;

    printf("%s", s);

    // initialize camera module
    res = hw_get_camera_module((const hw_module_t **)&module);
    assert(res == 0);
    printf("Camera module name : %s\n", module->name);
    printf("Camera module tag : %d\n", module->tag);
    printf("Camera module id : %s\n", module->id);
    module->open();

    while (1)
    {
        mqretcode = mq_receive(camera_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("camera_service_thread : You've got a message\n");
        printf("msg.type : %d\n", msg.msg_type);
        printf("msg.param1 : %d\n", msg.param1);
        printf("msg.param2 : %d\n", msg.param2);
        if (msg.msg_type == CAMERA_TAKE_PICTURE)
            module->take_picture();
        else if (msg.msg_type == DUMP_STATE)
            module->dump();
        else
            perror("camera_service_thread: unknown message.\n");
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
    pthread_t watchdog_thread_tid, monitor_thread_tid, disk_service_thread_tid, camera_service_thread_tid, timer_thread_tid;

    printf("나 system_server 프로세스!\n");

    watchdog_queue = mq_open("/watchdog_queue", O_RDWR);
    assert(watchdog_queue != -1);
    monitor_queue = mq_open("/monitor_queue", O_RDWR);
    assert(monitor_queue != -1);
    disk_queue = mq_open("/disk_queue", O_RDWR);
    assert(disk_queue != -1);
    camera_queue = mq_open("/camera_queue", O_RDWR);
    assert(camera_queue != -1);

    pthread_create(&watchdog_thread_tid, NULL, watchdog_thread_handler, NULL);
    pthread_create(&monitor_thread_tid, NULL, monitor_thread_handler, NULL);
    pthread_create(&disk_service_thread_tid, NULL, disk_service_thread_handler, NULL);
    pthread_create(&camera_service_thread_tid, NULL, camera_service_thread_handler, NULL);
    pthread_create(&timer_thread_tid, NULL, timer_thread_handler, NULL);

    pthread_detach(watchdog_thread_tid);
    pthread_detach(monitor_thread_tid);
    pthread_detach(disk_service_thread_tid);
    pthread_detach(camera_service_thread_tid);
    pthread_detach(timer_thread_tid);

    printf("system init done. waiting...\n");

    pthread_mutex_lock(&system_loop_mutex);
    while (system_loop_exit == false)
    {
        pthread_cond_wait(&system_loop_cond, &system_loop_mutex);
    }
    pthread_mutex_unlock(&system_loop_mutex);

    printf("<== system\n");

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
