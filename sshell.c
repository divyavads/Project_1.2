#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define CMDLINE_MAX 512
#define ARG_MAX 17
#define CMD_MAX 4

typedef struct {
    char *args[ARG_MAX];
    int argc;
} Command;

void check_bg_jobs(int *bg_ct, pid_t bg_pids[], int bg_retvals[], char bg_cmd[]) {
    if (*bg_ct == 0) return;
    int all_done = 1;
    for (int i = 0; i < *bg_ct; i++) {
        if (bg_pids[i] > 0) {
            int status;
            pid_t ret = waitpid(bg_pids[i], &status, WNOHANG);
            if (ret == 0) {
                all_done = 0;
            } else if (ret == bg_pids[i]) {
                if (WIFEXITED(status)) {
                    bg_retvals[i] = WEXITSTATUS(status);
                } else {
                    bg_retvals[i] = 1;
                }
                bg_pids[i] = 0;
            }
        }
    }
    if (all_done) {
        fprintf(stderr, "+ completed '%s' ", bg_cmd);
        for (int i = 0; i < *bg_ct; i++) {
            fprintf(stderr, "[%d]", bg_retvals[i]);
        }
        fprintf(stderr, "\n");
        *bg_ct = 0;
    }
}

int main(void)
{
    char cmd[CMDLINE_MAX];
    char *eof;

    int bg_ct = 0;
    pid_t bg_pids[CMD_MAX];
    int bg_retvals[CMD_MAX] = {0};
    char  bg_cmd[CMDLINE_MAX] = "";

    while(1){
        check_bg_jobs(&bg_ct, bg_pids, bg_retvals, bg_cmd);

        /* Print prompt */
        printf("sshell@ucd$ ");
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
            if (bg_ct > 0) {
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

        int is_background = 0;
        size_t len = strlen(cmd);
        if (len > 0 && cmd[len-1] == '&') {
            is_background = 1;
            cmd[--len] = '\0';
            if (len > 0 && cmd[len-1] == ' ') cmd[--len] = '\0';
        }
        if (strchr(cmd, '&')) {
            fprintf(stderr, "Error: mislocated background sign\n");
            continue;
        }

        if (strcmp(cmd, "pwd") == 0) {
            char cwd[CMDLINE_MAX];
            int ret = getcwd(cwd, sizeof(cwd)) ? 0 : 1;
            if (!ret) printf("%s\n", cwd);
            else perror("getcwd");
            fprintf(stderr, "+ completed '%s' [%d]\n", cmd_copy, ret);
            continue;
        }
        if (strncmp(cmd, "cd ", 3) == 0) {
            char *dest = cmd + 3;
            int ret = chdir(dest) ? 1 : 0;
            if (ret) fprintf(stderr, "Error: cannot cd into directory\n");
            fprintf(stderr, "+ completed '%s' [%d]\n", cmd_copy, ret);
            continue;
        }

        Command cmds[CMD_MAX];
        int ncmd = 0;
        char *input_file = NULL, *output_file = NULL;
        char *p;
        if ((p = strchr(cmd, '>'))) {
            *p = '\0'; 
            output_file = p+1;
            while (*output_file == ' ') 
                output_file++;
            char *e = strchr(output_file, ' ');
            if (e) 
                *e = '\0';
        }
        if ((p = strchr(cmd, '<'))) {
            *p = '\0'; 
            input_file = p+1;
            while (*input_file == ' ') 
                input_file++;
            char *e = strchr(input_file, ' ');
            if (e) 
                *e = '\0';
        }

        char *saveptr1;
        char *seg = strtok_r(cmd, "|", &saveptr1);
        while (seg && ncmd < CMD_MAX) {
            while (*seg == ' ') seg++;
            int L = strlen(seg);
            while (L>0 && seg[L-1]==' ') seg[--L]='\0';
            cmds[ncmd].argc = 0;
            char *saveptr2;
            char *tok = strtok_r(seg, " ", &saveptr2);
            while (tok && cmds[ncmd].argc < ARG_MAX-1) {
                cmds[ncmd].args[cmds[ncmd].argc++] = tok;
                tok = strtok_r(NULL, " ", &saveptr2);
            }
            cmds[ncmd].args[cmds[ncmd].argc] = NULL;
            if (cmds[ncmd].argc == 0) {
                fprintf(stderr, "Error: missing command\n");
                break;
            }
            ncmd++;
            seg = strtok_r(NULL, "|", &saveptr1);
        }

        if (seg) { 
            fprintf(stderr, "Error: too many commands\n"); 
            continue; 
        }

        if (ncmd == 0) 
            continue;

        if (input_file && ncmd>1) { 
            fprintf(stderr, "Error: mislocated input redirection\n"); 
            continue; 
        }
        int pipes[CMD_MAX-1][2];
        for (int i=0; i<ncmd-1; i++) 
            if (pipe(pipes[i])<0){
                perror("pipe");
                exit(1);
            } 

        pid_t pids[CMD_MAX];
        for (int i=0; i<ncmd; i++) {
            if ((pids[i]=fork())==0) {
                if (i==0 && input_file) {
                    int fd=open(input_file,O_RDONLY);
                    if(fd<0){
                        fprintf(stderr,"Error: cannot open input file\n");
                        exit(1);
                    } 
                    dup2(fd,STDIN_FILENO); 
                    close(fd);
                }
                if (i==ncmd-1 && output_file) {
                    int fd=open(output_file,O_WRONLY|O_CREAT|O_TRUNC,0644);
                    if(fd<0){
                        fprintf(stderr,"Error: cannot open output file\n");
                        exit(1);
                    } 
                    dup2(fd,STDOUT_FILENO); 
                    close(fd);
                }
                if (i>0) { 
                    dup2(pipes[i-1][0],STDIN_FILENO); 
                }
                if (i<ncmd-1) { 
                    dup2(pipes[i][1],STDOUT_FILENO); 
                }
                for(int j=0;j<ncmd-1;j++){
                    close(pipes[j][0]); 
                    close(pipes[j][1]);
                }
                execvp(cmds[i].args[0],cmds[i].args);
                fprintf(stderr,"Error: command not found\n");exit(1);
            }
        }
        for(int i=0;i<ncmd-1;i++){
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        if (is_background) {
            strcpy(bg_cmd, cmd_copy);
            for(int i=0;i<ncmd;i++) bg_pids[bg_ct++]=pids[i];
            continue;
        }
        int retvals[CMD_MAX];
        for(int i=0;i<ncmd;i++){
            int st; 
            waitpid(pids[i],&st,0);
            retvals[i] = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
        }

        check_bg_jobs(&bg_ct, bg_pids, bg_retvals, bg_cmd);
        fprintf(stderr,"+ completed '%s' ",cmd_copy);
        for(int i=0;i<ncmd;i++) fprintf(stderr,"[%d]",retvals[i]);
        fprintf(stderr,"\n");
    }
    return EXIT_SUCCESS;
}