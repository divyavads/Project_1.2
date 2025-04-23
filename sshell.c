#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define CMDLINE_MAX 512
#define ARG_MAX 17 

int main(void)
{
    char cmd[CMDLINE_MAX];
    char *eof;

    pid_t bg_pid = 0;
    int   bg_status = 0;
    char  bg_cmd[CMDLINE_MAX] = "";

    while (1) {
        if (bg_pid > 0) {
            pid_t ret = waitpid(bg_pid, &bg_status, WNOHANG);
            if (ret == bg_pid) {
                fprintf(stderr, "+ completed '%s' [%d]\n",
                        bg_cmd, WEXITSTATUS(bg_status));
                bg_pid = 0;
                bg_cmd[0] = '\0';
            }
        }

        /* Print prompt */
        printf("sshell$ ");
        fflush(stdout);

        /* Get command line */
        eof = fgets(cmd, CMDLINE_MAX, stdin);
        if (!eof)
            /* Make EOF equate to exit */
            strncpy(cmd, "exit\n", CMDLINE_MAX);

        if (!isatty(STDIN_FILENO)) {
            printf("%s", cmd);
            fflush(stdout);
        }

        /* Remove trailing newline from command line */
        char *nl = strchr(cmd, '\n');
        if (nl) *nl = '\0';

        /* Builtin exit */
        if (strcmp(cmd, "exit") == 0) {
            if (bg_pid > 0) {
                fprintf(stderr, "Error: active job still running\n");
                continue;
            }
            fprintf(stderr, "Bye...\n");
            fprintf(stderr, "+ completed 'exit' [0]\n");
            break;
        }

        if (strlen(cmd) == 0)
            continue;

        char cmd_copy[CMDLINE_MAX];
        strncpy(cmd_copy, cmd, CMDLINE_MAX);

        char *argv[ARG_MAX];
        int argc = 0;
        char *token = strtok(cmd, " ");
        while (token && argc < ARG_MAX - 1) {
            argv[argc++] = token;
            token = strtok(NULL, " ");
        }
        argv[argc] = NULL;

        int is_background = 0, mislocated = 0;
        if (argc > 0) {
            char *last = argv[argc-1];
            size_t len = strlen(last);
            if (len > 0 && last[len-1] == '&') {
                is_background = 1;
                if (len == 1) {
                    argv[argc-1] = NULL;
                    argc--;
                } else {
                    last[len-1] = '\0';
                }
            }
            for (int i = 0; i < argc; i++) {
                if (strchr(argv[i], '&')) {
                    mislocated = 1;
                    break;
                }
            }
        }

        if (mislocated) {
            fprintf(stderr, "Error: mislocated background sign\n");
            continue;
        }

        int retval = 0;
        if (argc > 0 && strcmp(argv[0], "pwd") == 0) {
            char cwd[CMDLINE_MAX];
            if (getcwd(cwd, sizeof(cwd))) {
                printf("%s\n", cwd);
            } else {
                perror("getcwd");
                retval = 1;
            }
            fprintf(stderr, "+ completed '%s' [%d]\n", cmd_copy, retval);
            continue;
        }
        if (argc > 0 && strcmp(argv[0], "cd") == 0) {
            if (argc < 2) {
                fprintf(stderr, "cd: missing operand\n");
                retval = 1;
            } else if (chdir(argv[1]) == -1) {
                fprintf(stderr, "Error: cannot cd into directory\n");
                retval = 1;
            }
            fprintf(stderr, "+ completed '%s' [%d]\n", cmd_copy, retval);
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) {
            execvp(argv[0], argv);
            fprintf(stderr, "Error: command not found\n");
            exit(1);
        } else {
            if (is_background) {
                bg_pid = pid;
                strncpy(bg_cmd, cmd_copy, CMDLINE_MAX);
            } else {
                waitpid(pid, &retval, 0);
                fprintf(stderr, "+ completed '%s' [%d]\n",
                        cmd_copy, WEXITSTATUS(retval));
            }
        }
    }

    return EXIT_SUCCESS;
}
