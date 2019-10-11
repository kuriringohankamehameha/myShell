#define _GNU_SOURCE

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<signal.h>
#include<fcntl.h>
#include<errno.h>
#include<sys/msg.h>
#include<sys/wait.h>
#include<sys/shm.h>
#include<syslog.h>

#define MAX_NUM_PATHS 100
#define uint32_t u_int32_t
#define COLOR_NONE "\033[m"
#define COLOR_RED "\033[1;37;41m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_CYAN "\033[0;36m"
#define COLOR_GREEN "\033[0;32;32m"
#define COLOR_GRAY "\033[1;30m"

/* Uncomment this line to get the status of each process from the Shell */
//#define DEBUG 1

enum bool {
    false = 0, true = ~0
};

enum _Status {
    STATUS_SUSPENDED,
    STATUS_BACKGROUND,
    STATUS_FOREGROUND
};

enum ptype {
    NONE,
    PIPE,
    MSG,
    SHM
};

typedef struct _Job_t Job;
typedef struct _Node_t Node;
typedef struct _Message_t Message;
typedef enum _Status Status;
typedef enum bool bool;
typedef enum ptype ptype;

bool isSuspended = false;

struct _Node_t {
    int pid;
    char * name;
    Status status;
    int gid;
};

struct _Job_t {
    Node node;
    Job * next;
};

struct _Message_t {
    long mtype;
    char mtext[8192];
};

/* Declare all Global Variables here */
char ** PATH; // PATH Variable
uint32_t pathLength = 0;
pid_t childPID, parentPID; // PIDs for Child and Parent Process
bool isBackground = false; // Checks whether the child is a background process
int bgcount = -1;
int argLength = 0;
char ** argVector; // Argument Vector being passed to the Shell
int redirection_in_pipe = 0; // For checking if there is an input redirection via pipe
int redirection_out_pipe = 0; // For chcking if there is an output redirection via pipe
int badRedirectpipe = 0; // Checks for valid Pipe redirection
int pipeCount = 0; // Counter for the number of Pipes in the argVector
bool redirect = false;
char ** redirectcmd1;
char ** redirectcmd2;
int pipeargCount = 0;
int lastPipe = 0; // Position of the last pipe symbol
char * USER; // Current User
char * HOME; // Home Directory
char * HOST;
int HOME_LEN;
bool ignorePATH = false; // For executing dot slash command
char ** command;
char ** * commands;
bool noinit_cmd = false;
char ** * msgOutputs;
char ** * shmOutputs;
int msgqueue = 0;
int shmemory = 0;
char * current_directory;
Job * jobSet; // Initialize the jobset
bool is_daemon = false; // For daemon checking
ptype pipeType = NONE;

/* Declare all Functions here */
Job * createJobs(Node node) {
    // Create the struct
    Job * head = (Job * ) malloc(sizeof(Job));
    head -> node = node;
    head -> next = NULL;

    return head;
}

Job * insert(Job * head, Node node) {
    // Insert a Job
    Job * newnode = (Job * ) malloc(sizeof(Job));
    newnode -> node = node;
    newnode -> next = NULL;

    Job * temp = head;

    if (temp -> next != NULL) {
        while (temp -> next)
            temp = temp -> next;
    }

    if (temp -> node.pid != node.pid)
        temp -> next = newnode;

    return head;

}

Job * removeJob(Job * head, int pid) {
    // Remove a Job
    if (isSuspended) {
        isSuspended = false;
        return head;
    }

    Job * temp = head;
    if (pid < 0 || temp -> node.pid == pid) {
        head = temp -> next;
        temp -> next = NULL;
        free(temp);
        return head;
    } else {
        // Remove PID
        while (temp -> next) {
            if (temp -> next -> node.pid == pid) {
                Job * del = temp -> next;
                temp -> next = del -> next;
                del -> next = NULL;
                free(del);
            } else
                temp = temp -> next;
        }
        return head;
    }
}

Job * findPID(Job * head, int pid) {
    Job * temp = head;
    while (temp) {
        if (temp -> node.pid == pid)
            return temp;
        temp = temp -> next;
    }

    return NULL;
}

int findFG_STATUS(Job * head) {
    // Returns the PID of the process in the FG that is not the parent
    Job * temp = head;
    temp = temp -> next;
    while (temp) {
        if (temp -> node.status == STATUS_FOREGROUND)
            return temp -> node.pid;
        temp = temp -> next;
    }

    return -1;
}

void printList(Job * head) {
    Job * temp = head;
    while (temp) {
        printf("pid = %d, name = %s, status = %d -> ", temp -> node.pid, temp -> node.name, temp -> node.status);
        temp = temp -> next;
    }
    printf("\n");
}

Job * changeStatus(Job * head, int pid, Status status) {
    // Change the status of a Job
    Job * temp = head;
    while (temp) {
        if (temp -> node.pid == pid) {
            temp -> node.status = status;
            return head;
        }
        temp = temp -> next;
    }
    return head;
}

void freeJobs(Job * jobSet) {
    Job * temp = jobSet;
    while (temp) {
        free(temp -> node.name);
        temp = temp -> next;
    }
}

void freePATH();
void printStatus();

static void shellHandler(int signo, siginfo_t * info, void * context) {
    if (signo == SIGCHLD) {
        Job * temp = findPID(jobSet, info -> si_pid);
        if (temp && temp -> node.status == STATUS_BACKGROUND) {
            printf("[bg]  %s - %d finished\n", temp -> node.name, info -> si_pid);
        }
        //printf("[bg] %d finished\n", info->si_pid);
        signal(SIGCHLD, SIG_DFL);
    }

}

void destroyGlobals() {
    // Destroy all globals
    free(current_directory);
    freePATH();
    freeJobs(jobSet);
}

char ** initDoubleGlobalCharPtr(int num_arrays) {
    char ** ptr = (char ** ) calloc(num_arrays, sizeof(char * ));
    for (int i = 0; i < num_arrays; i++)
        ptr[i] = (char * ) malloc(sizeof(char));
    return ptr;
}

void freeDoubleGlobalCharPtr(char ** * ptr, int num_arrays) {
    for (int i = 0; i < num_arrays; i++)
        free(( * ptr)[i]);
    free( * ptr);
    * ptr = NULL;
}

char ** * initTripleGlobalCharPtr(int num_arrays1, int num_arrays2) {
    char ** * ptr = (char ** * ) calloc(num_arrays1, sizeof(char ** ));
    for (int i = 0; i < num_arrays1; i++) {
        ptr[i] = (char ** ) calloc(num_arrays2, sizeof(char * ));
        for (int j = 0; j < num_arrays2; j++)
            ptr[i][j] = (char * ) malloc(sizeof(char));
    }
    return ptr;
}

void freeTripleGlobalCharPtr(char ** ** ptr, int num_arrays1, int num_arrays2) {
    for (int i = 0; i < num_arrays1 && ( * ptr)[i]; i++) {
        for (int j = 0; j < num_arrays2 && ( * ptr)[i][j]; j++)
            free(( * ptr)[i][j]);
        free(( * ptr)[i]);
    }
    free( * ptr);
    * ptr = NULL;
}

void replaceSubstring(char * str) {
    // For replacing $HOME with ~
    char * ptr = strstr(str, HOME);
    if (ptr) {
        current_directory[0] = '~';

        if (str[HOME_LEN] == '\0') {
            current_directory[1] = '\0';
            return;
        }

        for (int i = 1; str[i] != '\0'; i++) {
            if (str[i + HOME_LEN - 1] != '\0')
                current_directory[i] = str[i + HOME_LEN - 1];
            else {
                current_directory[i] = '\0';
                break;
            }
        }
        return;
    }
    return;
}

void printPrompt() {
    gethostname(HOST, 100);
    if (current_directory != NULL) {
        printf("%s%s@%s%s | %s%s%s > $ %s", COLOR_CYAN, USER, HOST, COLOR_GRAY, COLOR_RED, current_directory, COLOR_GREEN, COLOR_YELLOW);
    } else {
        char * s = (char * ) malloc(300 * sizeof(char));
        current_directory = getcwd(s, 300);
        printf("%s%s@%s%s | %s%s%s > $ %s", COLOR_CYAN, USER, HOST, COLOR_GRAY, COLOR_RED, current_directory, COLOR_GREEN, COLOR_YELLOW);
        free(s);
    }
}

char * str_concat(char * s1, char * s2) {
    int l1 = strlen(s1);
    int l2 = strlen(s2);
    char * op = (char * ) calloc(l1 + l2, sizeof(char));
    for (int i = 0; i < l1 + l2; i++)
        if (i < l1)
            op[i] = s1[i];
        else
            op[i] = s2[i - l1];
    return op;
}

uint32_t setPATH(const char * filename) {
    // Sets the PATH Variable from the config file
    // and returns it's length

    PATH = (char ** ) malloc(MAX_NUM_PATHS * sizeof(char * ));
    for (uint32_t i = 0; i < MAX_NUM_PATHS; i++)
        PATH[i] = (char * ) calloc(50, sizeof(char));

    char buffer[256];
    FILE * fp = fopen(filename, "r");
    uint32_t pathLength = 0;
    uint32_t j = 0;

    if (!fp) {
        printf("Error opening file '%s'\n", filename);
        exit(1);
    }

    while (fgets(buffer, sizeof(buffer), fp)) {
        for (uint32_t i = 0;; i++) {
            if (buffer[i] == '\n') {
                PATH[pathLength][j] = '\0';
                break;
            } else
                PATH[pathLength][j++] = buffer[i];
        }
        pathLength++;
        j = 0;
    }

    fclose(fp);

    return pathLength;
}

sigset_t createSignalSet(int * signalArray) {
    // Creates the Signal Set
    sigset_t set;
    sigemptyset( & set);
    for (int i = 0; signalArray[i] != -1; i++)
        sigaddset( & set, signalArray[i]);
    return set;
}

void freePATH() {
    // Release the PATH!!
    for (uint32_t i = 0; i < MAX_NUM_PATHS; i++)
        free(PATH[i]);
    free(PATH);
}

int builtin(char ** argVector) {
    // TODO (Vijay): Add more builtin commmands
    // 1. kill
    // 2. joblist

    // Execute dot slash commands
    if (argVector[1] == NULL && argVector[0][0] == '.' && argVector[0][1] == '/') {
        // Ignore PATH and simply execute from the current directory
        if (argVector[0][2] == '\0') {
            printf("myShell: Executabe must be a valid string\n");
            return 1;
        }

        argVector[0]++;
        ignorePATH = true;

        return 0;
    }

    // Change Directory Command
    // The Shell itself changes it's current working directory
    else if (argVector[1] != NULL && strcmp(argVector[0], "cd") == 0) {
        int pos = -1;
        for (int i = 0; argVector[1][i] != '\0'; i++) {
            if (argVector[1][0] == '~') {
                // Replace ~ with $HOME
                pos = i;
                break;
            }
        }
        if (pos != -1) {
            char * op = (char * ) calloc(300, sizeof(char));
            int k = 0;
            for (int i = 0; argVector[1][i] != '\0'; i++) {
                if (i == 0) {
                    strncpy(op, HOME, HOME_LEN);
                    k = k + HOME_LEN;
                } else
                    op[k++] = argVector[1][i];
            }
            op[k] = '\0';
            chdir(op);
            free(op);
        } else
            chdir(argVector[1]);

        char * s = (char * ) malloc(300 * sizeof(char));
        replaceSubstring(getcwd(s, 300));
        free(s);

        return 1;
    }

    return 0;
}

void shellkillCommand(int pid, int signo) {
    // Executes the Kill command on a process that is not the Parent or Child PID
    if (pid == getpid() || pid == getppid()) {
        printf("myShell: Cannot Kill parent process\n");
        return;
    }

    Job * temp = findPID(jobSet, pid);
    if (temp) {
        // Send a Signal to the child process based on the number 
        kill(pid, signo);
    }
}

void shellfgCommand(int pid) {
    Job * temp = findPID(jobSet, pid);
    kill(getpgid(pid), SIGTSTP);
    if (temp && temp -> node.status != STATUS_FOREGROUND) {
        // Bring the process to foreground
        // First, give the pid a CONT signal to resume from suspended state
        if (tcsetpgrp(0, pid) < 0)
            perror("tcsetpgrp()");

        if (kill(-pid, SIGCONT) < 0) {
            if (kill(pid, SIGCONT) < 0) {
                printf("myShell: fg %d: no such job\n", pid);
                return;
            }
        }

        // Make the pid get the terminal control
        printf("[fg]  + %d continued  %s\n", temp -> node.pid, temp -> node.name);

        // Remove the job from the jobSet
        removeJob(jobSet, pid);

        int status = 0;

        // Now the parent waits for the child to do it's job
        waitpid(pid, & status, WUNTRACED);
        signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(0, getpid());
        signal(SIGTTOU, SIG_DFL);
    } else
        printf("myShell: fg %d: no such job\n", pid);
}

void shellbgCommand(int pid) {
    // Resumes a previously suspended job in the background

    Job * temp = findPID(jobSet, pid);
    if (temp && temp -> node.pid != parentPID) {
        if (temp -> node.status != STATUS_SUSPENDED) {
            printf("myShell: bg %d: job not currently suspended. Cannot resume\n", pid);
            return;
        }

        if (kill(-pid, SIGCONT) < 0) {
            if (kill(pid, SIGCONT) < 0) {
                printf("myShell: bg %d: no such job\n", pid);
                return;
            }
        }

        printf("[bg]  + %d continued  %s &\n", temp -> node.pid, temp -> node.name);
        jobSet = changeStatus(jobSet, pid, STATUS_BACKGROUND);

        setpgid(0, 0);

        signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(0, getpid());
        signal(SIGTTOU, SIG_DFL);
    } else
        printf("myShell: bg %d: no such job\n", pid);

}

uint32_t getArgumentLength(char * command) {
    int count = 1;
    for (uint32_t i = 0; command[i] != '\0'; i++)
        if (command[i] == ' ' && command[i + 1] != ' ' && command[i + 1] != '\0')
            count++;
    return count;
}

void freeArgVector() {
    if (!ignorePATH)
        for (int i = 0; i <= argLength; i++)
            free(argVector[i]);
    free(argVector);
}

int getPIDfromname(Job * jobSet, char * name) {
    Job * temp = jobSet;
    while (temp) {
        if (strcmp(temp -> node.name, name) == 0)
            return temp -> node.pid;
        temp = temp -> next;
    }

    return 0;
}

FILE * popen_read() {
    // Emulates popen("cmd", "r")
    int fd[2];
    int read_fd, write_fd;
    int pid;

    pipe(fd);
    read_fd = fd[0];
    write_fd = fd[1];

    pid = fork();
    if (pid == 0) {

        close(read_fd);
        dup2(write_fd, 1);
        close(write_fd);

        int curr = 0;
        while (curr < pathLength) {
            if (execvp(str_concat(PATH[curr], command[0]), command) < 0)
                curr++;
        }

        printf("myShell: %s - Command not found\n", command[0]);
        return NULL;
    } else {
        close(write_fd);
        return fdopen(read_fd, "r");
    }
}

FILE * popen_write(char ** * cmd) {
    // Emulates popen("cmd", "w")
    int fd[2];
    int read_fd, write_fd;
    int pid;

    pipe(fd);
    read_fd = fd[0];
    write_fd = fd[1];

    pid = fork();
    if (pid == 0) {
        close(write_fd);
        dup2(read_fd, 0);
        close(read_fd);

        int curr = 0;
        while (curr < pathLength) {
            if (execvp(str_concat(PATH[curr], *(cmd)[0]), *(cmd)) < 0)
                curr++;
        }

        printf("myShell: %s - Command not found\n", *(cmd)[0]);
        return NULL;

    } else {
        close(read_fd);
        return fdopen(write_fd, "w");
    }
}

void executePipe(char ** * cmd) {
    // Execute a chain of Pipe | commands along with input/output redirection operators
    int p[2];
    int pid;
    int fd_in = 0;
    int file_fd_in = -1;
    int file_fd_out = -1;
    int save_in = dup(STDIN_FILENO);
    int save_out = dup(STDOUT_FILENO);

    if (redirection_in_pipe == 1) {
        for (int i = 0; cmd[0][i] != NULL; i++) {
            if (strcmp(cmd[0][i], "<") == 0) {
                if (cmd[0][i + 1] == NULL) {
                    printf("myShell: Redirection File does not exist\n");
                    return;
                }

                // Check if file exists
                if (access(cmd[0][i + 1], F_OK) == -1) {
                    printf("myShell: No such file or directory : %s\n", cmd[0][i + 1]);
                    return;
                }

                char * filename = (char * ) calloc(strlen(cmd[0][i + 1]) + 1, sizeof(char));
                strcpy(filename, cmd[0][i + 1]);
                file_fd_in = open(filename, O_RDONLY, 0400);

                cmd[0][i + 1] = 0;
                cmd[0][i] = 0;

                dup2(file_fd_in, STDIN_FILENO);
                close(file_fd_in);
            }
        }
    }

    if (redirection_out_pipe == 1) {
        for (int i = 0; cmd[pipeCount][i] != NULL; i++) {
            if (strcmp(cmd[pipeCount][i], ">") == 0 || strcmp(cmd[pipeCount][i], ">>") == 0) {
                if (cmd[pipeCount][i + 1] == NULL) {
                    printf("myShell: Redirection File does not exist\n");
                    return;
                }

                char * filename = (char * ) calloc(strlen(cmd[pipeCount][i + 1]) + 1, sizeof(char));
                strcpy(filename, cmd[pipeCount][i + 1]);

                if (strcmp(cmd[pipeCount][i], ">") == 0)
                    file_fd_out = open(filename, O_WRONLY | O_CREAT, 0644);
                else
                    file_fd_out = open(filename, O_WRONLY | O_APPEND | O_CREAT, 0644);

                free(filename);

                cmd[pipeCount][i + 1] = 0;
                cmd[pipeCount][i] = 0;
            }
        }
    }

    if (pipeType == PIPE) {
        while ( * cmd != NULL) {
            pipe(p);
            if ((pid = fork()) == -1) {
                printf("Error\n");
                exit(1);
            } else if (pid == 0) {
                dup2(fd_in, STDIN_FILENO);
                close(fd_in);
                if ( * (cmd + 1) != NULL)
                    dup2(p[1], STDOUT_FILENO);
                else {
                    if (redirection_out_pipe == 1) {
                        dup2(file_fd_out, STDOUT_FILENO);
                        close(file_fd_out);
                    }
                }

                close(p[0]);
                int curr = 0;
                while (curr < pathLength) {
                    if (execvp(str_concat(PATH[curr], ( * cmd)[0]), * cmd) < 0)
                        curr++;
                }
                exit(1);
            } else {
                wait(NULL);
                close(p[1]);
                fd_in = p[0];
                cmd++;
            }
        }
        if (redirection_in_pipe == 1) {
            dup2(save_in, STDIN_FILENO);
            close(save_in);
        }
        if (redirection_out_pipe == 1) {
            dup2(save_out, STDOUT_FILENO);
            close(save_out);
        }
    } else if (pipeType == MSG) {

        Message message;
        message.mtype = 23;
        memset( & (message.mtext), 0, 8192 * sizeof(char));
        int msgqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);

        FILE * input_pipe;
        int count = 0;
        input_pipe = stdin;

        while ( * cmd != NULL) {
            // Reveive STDIN from msgq and send stdout to the msgq
            int fd[2];
            pipe(fd);
            if (fork() == 0) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[1]);
                dup2(fd[0], STDIN_FILENO);
            }
            fclose(input_pipe);
            if ( * (cmd + 1) != NULL)
                input_pipe = fdopen(fd[0], "r");
            cmd++;
            count++;
        }

        msgctl(msgqid, IPC_RMID, NULL);
        if (redirection_in_pipe == 1) {
            dup2(save_in, STDIN_FILENO);
            close(save_in);
        }
        if (redirection_out_pipe == 1) {
            dup2(save_out, STDOUT_FILENO);
            close(save_out);
        }
    }
}

void execRedirect(int redirection) {
    // Performs the redirection into a file
    int fd = -1;
    int count1 = 0;
    int count2 = 0;
    for (int i = 0; redirectcmd1[i] != NULL; i++)
        count1++;
    for (int i = 0; redirectcmd2[i] != NULL; i++)
        count2++;

    if (count2 > 1) {
        printf("myShell: Can only redirect to a File\n");
        return;
    }

    if (redirection == 0) {
        // Output Redirection
        fd = open(redirectcmd2[0], O_WRONLY | O_CREAT, 0644);
    } else if (redirection == 1) {
        // Input Redirection
        if (count2 > 1) {
            printf("myShell: Can only redirect \'<\' from a File\n");
            return;
        }

        if (access(redirectcmd2[0], F_OK) == -1) {
            printf("myShell: No such file or directory : %s\n", redirectcmd2[0]);
            return;
        }

        fd = open(redirectcmd2[0], O_RDONLY, 0644);

        int pid = fork();
        int save_in = dup(STDIN_FILENO);
        if (pid == 0) {
            dup2(fd, STDIN_FILENO);
            close(fd);
            int curr = 0;
            while (curr < pathLength) {
                if (execvp(str_concat(PATH[curr], redirectcmd1[0]), redirectcmd1) < 0)
                    curr++;
            }
            fprintf(stderr, "Failed to execute %s\n", redirectcmd2[0]);
        } else {
            int status;
            waitpid(pid, & status, 0);
            dup2(save_in, STDIN_FILENO);
            close(save_in);
        }
        return;
    } else if (redirection == 2)
        fd = open(redirectcmd2[0], O_WRONLY | O_APPEND | O_CREAT, 0644);

    int pid = fork();
    int save_out = dup(STDOUT_FILENO);
    if (pid == 0) {
        dup2(fd, STDOUT_FILENO);
        close(fd);
        int curr = 0;
        while (curr < pathLength) {
            if (execvp(str_concat(PATH[curr], redirectcmd1[0]), redirectcmd1) < 0)
                curr++;
        }
        fprintf(stderr, "Failed to execute %s\n", redirectcmd1[0]);
    } else {
        int status;
        waitpid(pid, & status, 0);
        dup2(save_out, STDOUT_FILENO);
        close(save_out);
    }
}

void shellMsgQ(char ** * outputs) {
    // ls ## wc , sort using Message Queues
    int msgqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);

    // Create the message Queue
    if (msgqid == -1) {
        perror("Create msgqid");
        exit(0);
    }

    Message message;
    message.mtype = 23;
    memset( & (message.mtext), 0, 8192 * sizeof(char));

    if (fork() == 0) {
        FILE * cmd = popen_read();
        if (cmd == NULL) {
            exit(0);
        }
        int SIZE = 8192;
        char buffer[SIZE];

        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer) - 1, cmd)) > 0)
            buffer[n] = '\0';

        fclose(cmd);

        strcpy(message.mtext, buffer);

        if (msgsnd(msgqid, & message, sizeof(long) + (strlen(message.mtext) * sizeof(char)) + 1, 0) == -1) {
            perror("msgsnd");
            exit(0);
        }
        exit(0);
    }
    wait(NULL);

    if (msgrcv(msgqid, & message, 8192, 0, 0) == -1) {
        perror("msgrcv");
        return;
    }
    for (int i = 0; outputs[i] != NULL; i++) {
        if (fork() == 0) {
            FILE * inp = popen_write( & outputs[i]);
            if (inp == NULL) {
                exit(0);
            }
            fputs(message.mtext, inp);
            fclose(inp);
            exit(0);
        } else
            wait(NULL);
    }
    msgctl(msgqid, IPC_RMID, NULL);
}

void shellShm(char ** * outputs) {
    // ls SS wc , sort using Shared Memory
    int key = ftok("shmfile", 65);
    int shmid = shmget(key, 4096, IPC_CREAT | 0666);

    if (shmid == -1) {
        perror("shmid");
        exit(0);
    }

    if (fork() == 0) {
        char * str = (char * ) shmat(shmid, (void * ) 0, 0);
        FILE * cmd = popen_read();
        int SIZE = 8192;
        char buffer[SIZE];

        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer) - 1, cmd)) > 0)
            buffer[n] = '\0';

        pclose(cmd);

        strcpy(str, buffer);
        shmdt(str);

        exit(0);
    }
    wait(NULL);

    char * str = (char * ) shmat(shmid, (void * ) 0, 0);
    for (int i = 0; outputs[i] != NULL; i++) {
        if (fork() == 0) {
            char * output_cmd = (char * ) malloc(sizeof(char));
            for (int j = 0; outputs[i][j] != NULL; j++) {
                if (j == 0)
                    strcpy(output_cmd, outputs[i][j]);
                else {
                    output_cmd = str_concat(output_cmd, " ");
                    output_cmd = str_concat(output_cmd, outputs[i][j]);
                }
            }

            FILE * inp = popen_write( & outputs[i]);
            if (inp == NULL) {
                exit(0);
            }

            fputs(str, inp);
            pclose(inp);
            exit(0);
        } else
            wait(NULL);
    }

    shmdt(str);
    shmctl(shmid, IPC_RMID, NULL);
}

pid_t daemonizeProcess() {
    // Converts a process into a Daemon
    pid_t pid;
    pid_t session_id; // Session ID for the Daemon Process

    pid = fork();
    if (pid < 0) {
        printf("myShell: Unable to fork() to daemon process\n");
        return -1;
    } else if (pid == 0) {
        // Make sure that all log files used by the daemon can be accessed
        umask(0);
        session_id = setsid();
        if (session_id < 0) {
            printf("myShell: Failed to set the daemon session\n");
            exit(EXIT_FAILURE);
        }

        // Close all open file descriptors
        for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--)
            close(fd);

        // Open the Log file to be written by the daemon
        char * cmd = argVector[0];
        openlog(cmd, LOG_PID, LOG_DAEMON);

        int fd0 = open("/dev/null", O_RDWR);
        int fd1 = dup(0);
        int fd2 = dup(0);

        if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
            syslog(LOG_ERR, "Unexpected fds %d %d %d", fd0, fd1, fd2);
            closelog();
            exit(1);
        }

        for (;;) {
            /* Daemon Task */
            int curr = 0;
            while (curr < pathLength) {
                if (execv(str_concat(PATH[curr], argVector[0]), argVector) < 0)
                    curr++;
            }
            syslog(LOG_ERR, "Couldn't execute %s", argVector[0]);
            break;
        }
        closelog();
        exit(0);
    }
    return pid;
}

char * lineget(char * buffer) {
    if (buffer == NULL)
        buffer = malloc(300);

    char * line = buffer;
    char * linep = line;
    size_t lenmax = 300, len = lenmax;
    int c;

    if (line == NULL)
        return NULL;

    for (;;) {
        c = fgetc(stdin);
        if (c == EOF)
            break;

        if (--len == 0) {
            len = lenmax;
            char * linen = realloc(linep, lenmax *= 2);

            if (linen == NULL) {
                free(linep);
                return NULL;
            }
            line = linen + (line - linep);
            linep = linen;
        }

        if (( * line++ = c) == '\n')
            break;
    }
    * line = '\0';
    return linep;
}

int initArgs(char * buffer) {
    // Gets the argument length and copies all arguments as a list
    // to the argument vector
    int count = 0;
    int j = 0;
    for (uint32_t i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == ' ' && buffer[i + 1] == ' ')
            continue;
        else if (buffer[i] == ' ') {
            argVector[count][j++] = '\0';
            if (count == 0 && is_daemon == false && strcmp(argVector[0], "daemonize") == 0) {
                count = -1;
                is_daemon = true;
            }
            count++;
            j = 0;
        } else {
            argVector[count][j] = buffer[i];
            j++;
        }
    }

    argVector[count][j] = '\0';
    //free(argVector[count+1]);
    argVector[count + 1] = 0;
    return count;
}

void exec_fg() {
    int pid = -1;

    if (jobSet) {
        if (argVector[1][0] == '%' && argVector[1][1] != '\0') {
            // Search by name
            char * name = (char * ) malloc(sizeof(char));

            int i;
            for (i = 1; argVector[1][i] != '\0'; i++)
                name[i - 1] = argVector[1][i];
            name[i - 1] = '\0';

            pid = getPIDfromname(jobSet, name);
            if (!pid) {
                printf("myShell: fg %s: no such job\n", name);
                printPrompt();
                isBackground = false;
                return;
            }
            free(name);
        }
        shellfgCommand(pid == -1 ? atoi(argVector[1]) : pid);
        printPrompt();
        isBackground = false;
        return;
    } else {
        printf("No background jobs!\n");
        printPrompt();
        isBackground = false;
        return;
    }
}

void exec_bg() {
    // Gets the pid of the background job and sends it to shellbgcommand for execution
    int pid = -1;

    if (jobSet) {
        if (argVector[1][0] == '%' && argVector[1][2] != '\0') {
            // Search by name
            char * name = (char * ) malloc(sizeof(char));

            int i;
            for (i = 1; argVector[1][i] != '\0'; i++)
                name[i - 1] = argVector[1][i];
            name[i - 1] = '\0';

            pid = getPIDfromname(jobSet, name);
            if (!pid) {
                printf("myShell: bg %s: no such job\n", name);
                printPrompt();
                isBackground = false;
                return;
            }
            free(name);
        }
        shellbgCommand(pid == -1 ? atoi(argVector[1]) : pid);
        printPrompt();
        isBackground = false;
        return;
    } else {
        printf("No suspended jobs!\n");
        printPrompt();
        isBackground = false;
        return;
    }
}

void count_pipes() {
    // Counts the number of pipes in the chain
    int counter = 0;
    for (int i = 0; argVector[i] != NULL; i++) {
        if (strcmp(argVector[i], "|") == 0 || strcmp(argVector[i], "#") == 0 || strcmp(argVector[i], "S") == 0) {
            counter++;
            if (strcmp(argVector[i], "|") == 0)
                pipeType = PIPE;
            else if (strcmp(argVector[i], "#") == 0)
                pipeType = MSG;
            else
                pipeType = SHM;
        }
    }
    if (counter == 0)
        noinit_cmd = true;
    else
        commands = initTripleGlobalCharPtr(counter + 2, 20);
    return;
}

int pipe_filter() {
    int a = 0;

    // Get the number of pipe characters
    count_pipes();

    for (int i = 0; argVector[i] != NULL; i++) {
        pipeargCount++;
        if (strcmp(argVector[i], "|") == 0 || strcmp(argVector[i], "#") == 0 || strcmp(argVector[i], "S") == 0) {
            if (i > 2 && strcmp(argVector[i - 2], "<") == 0) {
                if (redirection_in_pipe == 0)
                    redirection_in_pipe = 1;
                else {
                    printf("myShell: Multiple Indirection Operators with Pipes\n");
                    badRedirectpipe = 1;
                    break;
                }

            }

            if (lastPipe == 0) {
                for (a = 0; a < i; a++) {
                    commands[pipeCount][a] = (char * ) realloc(commands[pipeCount][a], strlen(argVector[a]) + 1);
                    strcpy(commands[pipeCount][a], argVector[a]);
                }
                // Make NULL terminated command
                for (int a = i; a < 20; a++)
                    free(commands[pipeCount][a]);
                commands[pipeCount][i] = 0;

                // Update Pipe location
                lastPipe = i;
                pipeCount++;
            } else {
                for (a = lastPipe + 1; a < i; a++) {
                    commands[pipeCount][a - lastPipe - 1] = (char * ) realloc(commands[pipeCount][a - lastPipe - 1], strlen(argVector[a]) + 1);
                    strcpy(commands[pipeCount][a - lastPipe - 1], argVector[a]);
                }

                // Make NULL terminated command
                for (int a = i - lastPipe - 1; a >= 0 && a < 20; a++)
                    free(commands[pipeCount][a]);
                commands[pipeCount][i - lastPipe - 1] = 0;

                // Update Pipe location
                lastPipe = i;
                pipeCount++;
            }
        }
    }
    return a;
}

void output_redirection(int a) {
    // Execute the Pipe sequence after suitable redirection
    for (a = lastPipe + 1; argVector[a] != NULL; a++) {
        if (strcmp(argVector[a], "|") != 0 && strcmp(argVector[a], "#") != 0 && strcmp(argVector[a], "S") != 0) {
            commands[pipeCount][a - lastPipe - 1] = (char * ) realloc(commands[pipeCount][a - lastPipe - 1], strlen(argVector[a]) + 1);
            strcpy(commands[pipeCount][a - lastPipe - 1], argVector[a]);
        }
    }
    for (int b = a - lastPipe - 1; b < 20; b++)
        free(commands[pipeCount][b]);
    commands[pipeCount][a - lastPipe - 1] = 0;

    // Now, I must have a NULL terminated array of Commands
    for (int b = 0; b < 20; b++)
        free(commands[pipeCount + 1][b]);
    free(commands[pipeCount + 1]);
    commands[pipeCount + 1][a] = 0;
    commands[pipeCount + 1] = 0;

    if (pipeargCount >= 2 && (strcmp(argVector[pipeargCount - 2], ">") == 0 || strcmp(argVector[pipeargCount - 2], ">>") == 0)) {
        // a | b > c
        if (redirection_out_pipe == 0)
            redirection_out_pipe = 1;
        else {
            printf("myShell: Multiple Outdirection Operators with Pipes\n");
            badRedirectpipe = 1;
            return;
        }
    }

    executePipe(commands);
    return;
}

bool redirection_with_pipes_and_ipc() {
    // Performs any redirection after a sequence of pipes
    for (int i = 0; argVector[i] != NULL; i++) {
        if (strcmp(argVector[i], ">") == 0 || strcmp(argVector[i], ">>") == 0) {
            redirectcmd1 = (char ** ) calloc(50, sizeof(char * ));
            redirectcmd2 = (char ** ) calloc(50, sizeof(char * ));
            for (int i = 0; i < 5; i++) {
                redirectcmd1[i] = (char * ) malloc(sizeof(char));
                redirectcmd2[i] = (char * ) malloc(sizeof(char));
            }
            if (i <= lastPipe) {
                printf("myShell: Output Redirection before a pipe\n");
                redirect = true;
                return false;
            }

            for (int j = (lastPipe > 0) ? lastPipe + 1 : 0; j < i; j++) {
                if (lastPipe > 0)
                    strcpy(redirectcmd1[j - lastPipe - 1], argVector[j]);
                else
                    strcpy(redirectcmd1[j], argVector[j]);
            }
            if (lastPipe > 0)
                redirectcmd1[i - lastPipe - 1] = 0;
            else
                redirectcmd1[i] = 0;
            int j;
            for (j = i + 1; argVector[j] != NULL; j++)
                strcpy(redirectcmd2[j - i - 1], argVector[j]);
            redirectcmd2[j - i - 1] = 0;
            if (strcmp(argVector[i], ">") == 0)
                execRedirect(0);
            else
                execRedirect(2);
            for (int a = 0; a < 5; a++) {
                free(redirectcmd1[a]);
                free(redirectcmd2[a]);
            }
            free(redirectcmd1);
            free(redirectcmd2);
            redirect = true;
            return false;
        } else if (strcmp(argVector[i], "<") == 0) {
            redirectcmd1 = (char ** ) calloc(50, sizeof(char * ));
            redirectcmd2 = (char ** ) calloc(50, sizeof(char * ));
            for (int i = 0; i < 5; i++) {
                redirectcmd1[i] = (char * ) malloc(sizeof(char));
                redirectcmd2[i] = (char * ) malloc(sizeof(char));
            }
            for (int j = 0; j < i; j++)
                strcpy(redirectcmd1[j], argVector[j]);
            redirectcmd1[i] = 0;
            int j;
            for (j = i + 1; argVector[j] != NULL; j++)
                strcpy(redirectcmd2[j - i - 1], argVector[j]);
            redirectcmd2[j - i - 1] = 0;
            execRedirect(1);
            for (int a = 0; a < 5; a++) {
                free(redirectcmd1[a]);
                free(redirectcmd2[a]);
            }
            free(redirectcmd1);
            free(redirectcmd2);
            redirect = true;
            return false;
        }

        // As of now, there's no support for Msg Queues with Pipes / Redirection
        else if (strcmp(argVector[i], "##") == 0) {
            msgOutputs = initTripleGlobalCharPtr(20, 5);
            int * array = (int * ) malloc(sizeof(int));
            command = initDoubleGlobalCharPtr(i + 1);
            for (int j = 0; j < i; j++)
                strcpy(command[j], argVector[j]);
            free(command[i]);
            command[i] = NULL;

            int k = 0;
            int l = 0;

            for (int j = i + 1; argVector[j] != NULL; j++) {
                if (strcmp(argVector[j], ",") == 0) {
                    msgOutputs[k][l++] = 0;
                    array[k] = l;
                    k++;
                    l = 0;
                } else {
                    strcpy(msgOutputs[k][l++], argVector[j]);
                    if (argVector[j + 1] == NULL) {
                        msgOutputs[k][l] = 0;
                        if (l < 5)
                            for (int q = l; q < 5; q++)
                                free(msgOutputs[k][q]);
                    }
                }
                array[k] = l - 1;
            }
            for (int q = k + 1; q < 20; q++) {
                for (int r = 0; r < 5; r++)
                    free(msgOutputs[q][r]);
                free(msgOutputs[q]);
            }
            msgOutputs[k + 1] = 0;
            shellMsgQ(msgOutputs);
            for (int p = 0; p <= k; p++) {
                for (int q = 0; q < array[p]; q++)
                    free(msgOutputs[p][q]);
                free(msgOutputs[p]);
            }
            free(msgOutputs);
            free(array);
            freeDoubleGlobalCharPtr( & command, i + 1);
            return true;
        } else if (strcmp(argVector[i], "SS") == 0) {
            shmOutputs = initTripleGlobalCharPtr(20, 5);
            int * array = (int * ) malloc(sizeof(int));
            command = initDoubleGlobalCharPtr(i + 1);
            for (int j = 0; j < i; j++)
                strcpy(command[j], argVector[j]);
            free(command[i]);
            command[i] = NULL;

            int k = 0;
            int l = 0;

            for (int j = i + 1; argVector[j] != NULL; j++) {
                if (strcmp(argVector[j], ",") == 0) {
                    shmOutputs[k][l++] = 0;
                    array[k] = l;
                    k++;
                    l = 0;
                } else {
                    strcpy(shmOutputs[k][l++], argVector[j]);
                    if (argVector[j + 1] == NULL) {
                        shmOutputs[k][l] = 0;
                        if (l < 5)
                            for (int q = l; q < 5; q++)
                                free(shmOutputs[k][q]);
                    }
                }
                array[k] = l;
            }
            for (int q = k + 1; q < 20; q++) {
                for (int r = 0; r < 5; r++)
                    free(shmOutputs[q][r]);
                free(shmOutputs[q]);
            }
            shmOutputs[k + 1] = 0;
            shellShm(shmOutputs);
            for (int p = 0; p <= k; p++) {
                for (int q = 0; q < array[p]; q++)
                    free(shmOutputs[p][q]);
                free(shmOutputs[p]);
            }
            free(shmOutputs);
            free(array);
            freeDoubleGlobalCharPtr( & command, i + 1);
            return true;
        }
    }
    return false;
}

void insert_background_job(pid_t pid) {
    // Insert the background job into the jobSet and wait for completion
    Node node;
    node.pid = pid;
    node.status = STATUS_BACKGROUND;
    node.gid = pid;
    node.name = (char * ) malloc(sizeof(char));
    strcpy(node.name, argVector[0]);
    jobSet = insert(jobSet, node);

    #ifdef DEBUG
    printStatus(pid, STATUS_BACKGROUND);
    #endif

    setpgrp();
    int status;
    if (waitpid(pid, & status, WNOHANG) > 0)
        printf("[bg]  %d finished\n", pid);
}

void job_wait(pid_t pid) {
    // Waits for a job
    int status;
    waitpid(pid, & status, WUNTRACED);
    if (WSTOPSIG(status)) {
        setpgid(pid, pid);
        char * name = (char * ) malloc(sizeof(char));
        strcpy(name, argVector[0]);
        Node node = {
            pid,
            name,
            STATUS_SUSPENDED,
            getpgrp()
        };
        jobSet = insert(jobSet, node);

        #ifdef DEBUG
        printStatus(pid, STATUS_SUSPENDED);
        #endif

        printf("\nSuspended  %s %d\n", node.name, node.pid);
        //signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(0, getpid());
        tcsetpgrp(1, getpid());
        //signal(SIGTTOU, SIG_DFL);
    }
}

void execute_child(sigset_t set, int pathLength) {
    // The child must receive the previously blocked signals
    sigprocmask(SIG_UNBLOCK, & set, NULL);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);

    if (isBackground == true) {
        // Make the process have it's own group id
        printf("[bg]  %d\n", getpid());
        setpgid(0, 0);
    } else {
        // Add to foreground process group
        pid_t cgrp = getpgrp();
        tcsetpgrp(STDIN_FILENO, cgrp);
        setpgid(getpgrp(), getppid());
    }

    childPID = getpid();
    int curr = 0;

    if (ignorePATH == false) {
        while (curr < pathLength) {
            // Keep going through the PATH until we can execute one program
            if (execv(str_concat(PATH[curr], argVector[0]), argVector) < 0) {
                curr++;
                if (curr == pathLength) {
                    perror("Error");
                    exit(0);
                }
            }
        }

    } else {
        ignorePATH = false;
        char s[300];
        if (execv(str_concat(getcwd(s, 300), argVector[0]), argVector) < 0) {
            perror("Error");
            exit(0);
        }
    }
}

void initStuff(char * name) {
    // Initialise jobSet
    jobSet = (Job * ) malloc(sizeof(Job));
    Node jnode;
    jnode.pid = getpid();
    jnode.name = (char * ) calloc(150, sizeof(char));
    strcpy(jnode.name, name);
    jnode.status = STATUS_FOREGROUND;
    jnode.gid = getpid();
    jobSet -> node = jnode;
    jobSet -> next = NULL;

    // Get some Environment Variables for pretty prompt
    USER = getenv("USER");
    HOME = getenv("HOME");
    HOST = (char * ) malloc(100 * sizeof(char));
    HOME_LEN = strlen(HOME);
}

void printStatus(pid_t pid, Status status) {
    printf("PID : %d, ", pid);
    switch (status) {
    case (STATUS_BACKGROUND):
        printf("Status : STATUS_BACKGROUND\n");
        break;
    case (STATUS_FOREGROUND):
        printf("Status : STATUS_FOREGROUND\n");
        break;
    case (STATUS_SUSPENDED):
        printf("Status : STATUS_SUSPENDED\n");
        break;
    default:
        break;
    }
}

int main(int argc, char * argv[]) {

    #ifdef DEBUG
    if (argc > 1) {
        printf("Fatal : Must invoke without extra arguments\n");
        exit(EXIT_FAILURE);
    }
    #endif

    // Get the length of the config file for search and execute
    pathLength = setPATH(".myshellrc");
    char * buffer = NULL;
    int count;
    current_directory = (char * ) calloc(300, sizeof(char));

    // Initialise stuff ...
    initStuff(argv[0]);

    // Create the signal array for blocking/unblocking
    // The main process needs to block, while the children can unblock
    // the signals
    struct sigaction sa = {
        0
    };
    sa.sa_sigaction = shellHandler;
    sa.sa_flags = SA_SIGINFO;

    int signalArray[] = {
        SIGINT,
        SIGKILL,
        SIGTSTP,
        -1
    };
    sigset_t set = createSignalSet(signalArray);

    // Apply the mask
    sigprocmask(SIG_BLOCK, & set, NULL);

    // We need to find the Current Directory before printing the prompt
    char * s = (char * ) malloc(300 * sizeof(char));
    replaceSubstring(getcwd(s, 300));
    free(s);

    printPrompt();

    // Now start the event loop
    while ((buffer = lineget(buffer))) {
        pipeType = NONE;
        if (strcmp(buffer, "\n") == 0) {
            printPrompt();
            continue;
        }
        // Enable SIGCHLD Handler for capturing status of bg processes
        sigaction(SIGCHLD, & sa, NULL);

        // Get the argument length
        argLength = getArgumentLength(buffer);
        argVector = (char ** ) malloc((argLength + 1) * sizeof(char * ));
        for (int i = 0; i <= argLength; i++)
            argVector[i] = (char * ) malloc(sizeof(char));

        redirection_in_pipe = 0;
        redirection_out_pipe = 0;
        pipeCount = 0;
        pipeargCount = 0;
        bgcount = -1;

        for (uint32_t i = 0; buffer[i] != '\0'; i++)
            if (buffer[i] == '\n')
                buffer[i] = '\0';

        Job * curr = jobSet;
        while (curr) {
            if (curr -> node.status == STATUS_BACKGROUND && waitpid(curr -> node.pid, NULL, WNOHANG)) { // Background Job has returned
                jobSet = removeJob(jobSet, curr -> node.pid);
                curr = jobSet;
            }
            curr = curr -> next;
        }

        if (strcmp(buffer, "exit") == 0) {
            destroyGlobals();
            free(jobSet);
            free(buffer);
            freeArgVector();
            free(HOST);
            exit(0);
        }

        if (strcmp(buffer, "printlist") == 0) {
            printList(jobSet);
            freeArgVector();
            printPrompt();
            continue;
        } else {
            count = initArgs(buffer);

            if (builtin(argVector) == 1) {
                // Execute a builtin command
                freeArgVector();
                printPrompt();
                continue;
            }

            if (strcmp(argVector[count], "&") == 0) {
                // Needs to be a Background process
                isBackground = true;
                bgcount = count;
                for (int i = count; i < argLength; i++)
                    free(argVector[i]);
                argVector[count] = 0;
            } else if (strcmp(argVector[0], "fg") == 0) {
                // fg command
                exec_fg();
                freeArgVector();
                printPrompt();
                continue;
            } else if (strcmp(argVector[0], "bg") == 0) {
                // bg command
                exec_bg();
                freeArgVector();
                printPrompt();
                continue;
            } else if (is_daemon) {
                // Daemonize command
                if (argVector) {
                    int daemon_pid = daemonizeProcess();
                    daemon_pid != 1 ? printf("Daemon started with PID = %d\n", daemon_pid) : printf("myShell: Daemon initialization failed\n");
                } else
                    printf("myShell: daemonize needs atleast one command\n");
                is_daemon = false;
                freeArgVector();
                printPrompt();
                continue;
            }

            int a = pipe_filter();

            if (badRedirectpipe == 1) {
                // Go back to the start of the REPL
                freeArgVector();
                printPrompt();
                if (noinit_cmd == false)
                    freeTripleGlobalCharPtr( & commands, pipeCount + 2, 20);
                noinit_cmd = false;
                lastPipe = 0;
                continue;
            }

            if (pipeCount > 0) {
                output_redirection(a);
                freeArgVector();
                if (noinit_cmd == false)
                    freeTripleGlobalCharPtr( & commands, pipeCount + 2, 20);
                noinit_cmd = false;
                lastPipe = 0;
                printPrompt();
                continue;
            }

            bool result = redirection_with_pipes_and_ipc(redirectcmd1, redirectcmd2);

            if (result == true || redirect == true) {
                redirect = false;
                freeArgVector();
                printPrompt();
                if (noinit_cmd == false)
                    freeTripleGlobalCharPtr( & commands, pipeCount + 2, 20);
                noinit_cmd = false;
                lastPipe = 0;
                continue;
            }

            int pid = fork();
            childPID = pid;

            if (pid == 0)
                execute_child(set, pathLength);

            else {
                parentPID = getpid();
                setpgid(0, 0);
                if (isBackground) {
                    insert_background_job(pid);
                } else {

                    #ifdef DEBUG
                    printStatus(pid, STATUS_FOREGROUND);
                    #endif
                    
                    job_wait(pid);
                }

                if (isBackground) {
                    for (int i = 0; bgcount >= 0 && i < bgcount; i++)
                        free(argVector[i]);
                    free(argVector);
                } else
                    freeArgVector();
                isBackground = false;
                noinit_cmd = false;
                lastPipe = 0;
                ignorePATH = false;
                printPrompt();
            }
        }
    }
    return 0;
}
