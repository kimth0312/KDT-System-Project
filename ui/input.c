#include <stdio.h>
#include <sys/prctl.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <mqueue.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <execinfo.h>
#include <sys/wait.h>
#include <toy_message.h>

#define TOY_TOK_BUFSIZE 64
#define TOY_TOK_DELIM " \t\r\n\a"
#define TOY_BUFFSIZE 1024

int toy_send(char **args);
int toy_shell(char **args);
int toy_exit(char **args);
int toy_mutex(char **args);
int toy_message_queue(char **args);

typedef struct _sig_ucontext
{
    unsigned long uc_flags;
    struct ucontext *uc_link;
    stack_t uc_stack;
    struct sigcontext uc_mcontext;
    sigset_t uc_sigmask;
} sig_ucontext_t;

static pthread_mutex_t global_message_mutex = PTHREAD_MUTEX_INITIALIZER;
static char global_message[TOY_BUFFSIZE];

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;

void segfault_handler(int sig_num, siginfo_t *info, void *ucontext)
{
    void *array[50];
    void *caller_address;
    char **messages;
    int size, i;
    sig_ucontext_t *uc;

    uc = (sig_ucontext_t *)ucontext;

    // fetch the rip when the signal is raised.
    caller_address = (void *)uc->uc_mcontext.rip;

    fprintf(stderr, "\n");

    if (sig_num == SIGSEGV)
        printf("signal %d (%s), address is %p from %p\n", sig_num, strsignal(sig_num), info->si_addr,
               (void *)caller_address);
    else
        printf("signal %d (%s)\n", sig_num, strsignal(sig_num));

    size = backtrace(array, 50);
    array[1] = caller_address;
    messages = backtrace_symbols(array, size);

    for (i = 1; i < size && messages != NULL; ++i)
        printf("[bt] : (%d) %s\n", i, messages[i]);

    free(messages);

    exit(EXIT_FAILURE);
}

// sensor thread
void *sensor_thread(void *arg)
{
    char *s = (char *)arg;

    printf("%s\n", s);

    while (1)
    {
        sleep(5);
    }

    return 0;
}

// command threads
char *builtin_str[] = {
    "send",
    "sh",
    "exit",
    "mu",
    "mq"};

// declare functions in an array
int (*builtin_func[])(char **) = {
    &toy_send,
    &toy_shell,
    &toy_exit,
    &toy_mutex,
    &toy_message_queue};

int toy_num_builtins()
{
    return sizeof(builtin_str) / sizeof(char *);
}

int toy_send(char **args)
{
    printf("send messsage: %s\n", args[1]);

    return 1;
}

int toy_mutex(char **args)
{
    if (args[1] == NULL)
    {
        return 1;
    }

    printf("save message: %s\n", args[1]);
    // mutex
    pthread_mutex_lock(&global_message_mutex);
    strcpy(global_message, args[1]);
    pthread_mutex_unlock(&global_message_mutex);
    return 1;
}

int toy_message_queue(char **args)
{
    int mqretcode;
    toy_msg_t msg;

    if (args[1] == NULL || args[2] == NULL)
        return 1;

    if (!strcmp(args[1], "camera"))
    {
        msg.msg_type = atoi(args[2]);
        msg.param1 = 0, msg.param2 = 0;
        mqretcode = mq_send(camera_queue, (char *)&msg, sizeof(msg), 0);
        assert(mqretcode == 0);
    }

    return 1;
}

int toy_exit(char **args)
{
    return 0;
}

int toy_shell(char **args)
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid == 0)
    {
        if (execvp(args[0], args) == -1)
        {
            perror("toy_shell");
        }
        exit(EXIT_FAILURE);
    }
    else if (pid < 0)
        perror("toy");
    else
    {
        do
        {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status)); // when a process stays alive && didn't get any signal
    }

    return 1;
}

int toy_execute(char **args)
{
    int i;

    if (args[0] == NULL)
        return 1;

    for (i = 0; i < toy_num_builtins(); i++)
    {
        if (strcmp(args[0], builtin_str[i]) == 0)
            return (*builtin_func[i])(args);
    }
}

char *toy_read_line(void)
{
    char *line = NULL;
    size_t bufsize = 0;

    if (getline(&line, &bufsize, stdin) == -1)
    {
        if (feof(stdin))
            exit(EXIT_SUCCESS);
        else
        {
            perror(": getline\n");
            exit(EXIT_FAILURE);
        }
    }
    return line;
}

char **toy_split_line(char *line)
{
    int bufsize = TOY_TOK_BUFSIZE, position = 0;
    char **tokens = (char **)malloc(bufsize * sizeof(char *));
    char *token, **tokens_backup;

    if (!tokens)
    {
        fprintf(stderr, "toy: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOY_TOK_DELIM);
    while (token != NULL)
    {
        tokens[position] = token;
        position++;

        if (position >= bufsize)
        {
            bufsize += TOY_TOK_BUFSIZE;
            tokens_backup = (char **)realloc(tokens, bufsize * sizeof(char *));
            if (!tokens)
            {
                free(tokens_backup);
                fprintf(stderr, "toy: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOY_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

void toy_loop(void)
{
    char *line;
    char **args;
    int status;

    do
    {
        // mutex
        printf("TOY>");
        line = toy_read_line();
        args = toy_split_line(line);
        status = toy_execute(args);

        free(line);
        free(args);
    } while (status);
}

void *command_thread(void *arg)
{
    char *s = (char *)arg;
    printf("%s\n", s);
    toy_loop();
    return 0;
}

#define MAX 30
#define NUMTHREAD 3

char buffer[TOY_BUFFSIZE];
int read_count = 0, write_count = 0;
int buflen;
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
int thread_id[NUMTHREAD] = {0, 1, 2};
int producer_count = 0, consumer_count = 0;

void *toy_consumer(int *id)
{
    pthread_mutex_lock(&count_mutex);
    while (consumer_count < MAX)
    {
        pthread_cond_wait(&empty, &count_mutex);
        printf("                           소비자[%d]: %c\n", *id, buffer[read_count]);
        read_count = (read_count + 1) % TOY_BUFFSIZE;
        fflush(stdout);
        consumer_count++;
    }
    pthread_mutex_unlock(&count_mutex);
}

void *toy_producer(int *id)
{
    while (producer_count < MAX)
    {
        pthread_mutex_lock(&count_mutex);
        strcpy(buffer, "");
        buffer[write_count] = global_message[write_count % buflen];
        printf("%d - 생산자[%d]: %c \n", producer_count, *id, buffer[write_count]);
        fflush(stdout);
        write_count = (write_count + 1) % TOY_BUFFSIZE;
        producer_count++;
        pthread_cond_signal(&empty);
        pthread_mutex_unlock(&count_mutex);
        sleep(rand() % 3);
    }
}

int input()
{
    int retcode;
    struct sigaction sa;
    pthread_t command_thread_tid, sensor_thread_tid;
    int i;
    pthread_t thread[NUMTHREAD];

    printf("나 input 프로세스!\n");

    // SIGSEGV 시그널 등록
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_sigaction = segfault_handler;
    if (sigaction(SIGSEGV, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(-1);
    }

    watchdog_queue = mq_open("/watchdog_queue", O_RDWR);
    assert(watchdog_queue != -1);
    monitor_queue = mq_open("/monitor_queue", O_RDWR);
    assert(monitor_queue != -1);
    disk_queue = mq_open("/disk_queue", O_RDWR);
    assert(disk_queue != -1);
    camera_queue = mq_open("/camera_queue", O_RDWR);
    assert(camera_queue != -1);

    // create threads
    pthread_create(&command_thread_tid, NULL, command_thread, (void *)"command thread operates");
    pthread_create(&sensor_thread_tid, NULL, sensor_thread, (void *)"sensor thread operates");

    pthread_detach(command_thread_tid);
    pthread_detach(sensor_thread_tid);
    /*
        pthread_mutex_lock(&global_message_mutex);
        strcpy(global_message, "hello world!");
        buflen = strlen(global_message);
        pthread_mutex_unlock(&global_message_mutex);
        pthread_create(&thread[0], NULL, (void *)toy_consumer, &thread_id[0]);
        pthread_create(&thread[1], NULL, toy_producer, &thread_id[1]);
        pthread_create(&thread[2], NULL, toy_producer, &thread_id[2]);

        for (i = 0; i < NUMTHREAD; i++)
            pthread_join(thread[i], NULL);
    */
    while (1)
    {
        sleep(1);
    }

    return 0;
}

int create_input()
{
    pid_t systemPid;
    const char *name = "input";

    printf("여기서 input 프로세스를 생성합니다.\n");

    if ((systemPid = fork()) < 0)
    {
        perror("input fork error");
        exit(-1);
    }
    else if (systemPid == 0)
    {
        if (prctl(PR_SET_NAME, (unsigned long)name) < 0)
            perror("prctl()");
        input();
    }

    return 0;
}
