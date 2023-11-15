#include <stdio.h>
#include <sys/wait.h>
#include <mqueue.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <signal.h>
#include <sys/wait.h>
#include <toy_message.h>
#include <assert.h>

#define NUM_MESSAGES 10

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;

static void sigchldHandler(int sig)
{
    int status, savedErrno;
    pid_t childPid;

    savedErrno = errno;

    printf("handler : Caught SIGCHLD : 17\n");
    printf("handler : returning\n");

    errno = savedErrno;
}

int create_message_queue(mqd_t *msgq_ptr, const char *queue_name, int num_messages, int message_size)
{
    struct mq_attr mq_attrib;
    int mq_errno;
    mqd_t msgq;

    printf("%s name=%s nummsgs=%d\n", __func__, queue_name, num_messages);

    memset(&mq_attrib, 0, sizeof(mq_attrib));
    mq_attrib.mq_msgsize = message_size;
    mq_attrib.mq_maxmsg = num_messages;

    mq_unlink(queue_name);
    msgq = mq_open(queue_name, O_RDWR | O_CREAT | O_CLOEXEC, 0777, &mq_attrib);
    if (msgq == -1)
    {
        printf("%s queue=%s already exists so try to open\n",
               __func__, queue_name);
        msgq = mq_open(queue_name, O_RDWR);
        assert(msgq != (mqd_t)-1);
        printf("%s queue=%s opened successfully\n",
               __func__, queue_name);
        return -1;
    }

    *msgq_ptr = msgq;
    return 0;
}

int main()
{
    pid_t spid, gpid, ipid, wpid;
    int status;
    int sigCnt;
    sigset_t blockMask, emptyMask;
    struct sigaction sa;
    int retcode;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigchldHandler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(-1);
    }

    printf("메인 함수입니다.\n");
    /*
        watchdog_queue = mq_open("/watchdog_queue", O_CREAT | O_RDWR | O_EXCL);
        monitor_queue = mq_open("/monitor_queue", O_CREAT | O_RDWR | O_EXCL);
        disk_queue = mq_open("/disk_queue", O_CREAT | O_RDWR | O_EXCL);
        camera_queue = mq_open("/camera_queue", O_CREAT | O_RDWR | O_EXCL);
    */
    retcode = create_message_queue(&watchdog_queue, "/watchdog_queue", NUM_MESSAGES, sizeof(toy_msg_t));
    assert(retcode == 0);
    retcode = create_message_queue(&monitor_queue, "/monitor_queue", NUM_MESSAGES, sizeof(toy_msg_t));
    assert(retcode == 0);
    retcode = create_message_queue(&disk_queue, "/disk_queue", NUM_MESSAGES, sizeof(toy_msg_t));
    assert(retcode == 0);
    retcode = create_message_queue(&camera_queue, "/camera_queue", NUM_MESSAGES, sizeof(toy_msg_t));
    assert(retcode == 0);

    printf("시스템 서버를 생성합니다.\n");
    spid = create_system_server();
    printf("웹 서버를 생성합니다.\n");
    wpid = create_web_server();
    printf("입력 프로세스를 생성합니다.\n");
    ipid = create_input();
    printf("GUI를 생성합니다.\n");
    gpid = create_gui();

    waitpid(spid, &status, 0);
    waitpid(gpid, &status, 0);
    waitpid(ipid, &status, 0);
    waitpid(wpid, &status, 0);

    printf("I finished\n");

    return 0;
}
