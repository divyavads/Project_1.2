#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define CMDLINE_MAX 512
#define AARG_MAX 18
#define CMD_MAX 4

typedef struct {
    char *args[AARG_MAX];
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

        check_bg_jobs(&bg_ct, bg_pids, bg_retvals, bg_cmd);

        /* Builtin exit */
        if (strcmp(cmd, "exit") == 0) {
            check_bg_jobs(&bg_ct, bg_pids, bg_retvals, bg_cmd);
            if (bg_ct > 0) {
                fprintf(stderr, "Error: active job still running\n");
                fprintf(stderr, "+ completed 'exit' [1]\n");
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
            check_bg_jobs(&bg_ct, bg_pids, bg_retvals, bg_cmd);
            if(bg_ct > 0) {
                fprintf(stderr, "Error: active job still running\n");
                continue;
            }
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
        if (strncmp(cmd, "cd", 2) == 0) {
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
            if (*output_file=='\0') { fprintf(stderr,"Error: no output file\n"); continue; }
        }
        if ((p = strchr(cmd, '<'))) {
            *p = '\0'; 
            input_file = p+1;
            while (*input_file == ' ') 
                input_file++;
            char *e = strchr(input_file, ' ');
            if (e) 
                *e = '\0';
             if (*input_file=='\0') { fprintf(stderr,"Error: no input file\n"); continue; }
        }

        if (output_file) {
            char *p_orig = strchr(cmd_copy, '>');
            char *last_pipe = strrchr(cmd_copy, '|');
            if (last_pipe && p_orig < last_pipe) {
                fprintf(stderr, "Error: mislocated output redirection\n");
                continue;
            }
            
            if (!last_pipe) {
                last_pipe = cmd_copy;
            }
            char *check = last_pipe;
            while (check < p_orig) {
                if (*check != ' ' && *check != '|') {
                    break;
                }
                check++;
            }
            if (check == p_orig) {
                fprintf(stderr, "Error: missing command\n");
                continue;
            }
        }
        if (input_file) {
            char *p_in = strchr(cmd_copy, '<');
            char *first_pipe = strchr(cmd_copy, '|');
            if (first_pipe && p_in > first_pipe) {
                fprintf(stderr, "Error: mislocated input redirection\n");
                continue;
            }
            char *check = cmd_copy;
            while (check < p_in) {
                if (*check != ' ') {
                    break;
                }
                check++;
            }
            if (check == p_in) {
                fprintf(stderr, "Error: missing command\n");
                continue;
            }
        }

        char tmp[CMDLINE_MAX]; 
        strncpy(tmp, cmd, CMDLINE_MAX);
        // trim spaces
        char *ts = tmp; while (*ts==' ') ts++;
        char *te = ts + strlen(ts) - 1;
        while (te>ts && *te==' ') *te--='\0';
        if (ts[0]=='|' || *te=='|' || strstr(ts, "||")) {
            fprintf(stderr, "Error: missing command\n");
            continue;
        }

        char *saveptr1;
        char *seg = strtok_r(cmd, "|", &saveptr1);
        int error = 0;
        while (seg && ncmd < CMD_MAX) {
            while (*seg == ' ') seg++;
            int L = strlen(seg);
            while (L>0 && seg[L-1]==' ') seg[--L]='\0';
            cmds[ncmd].argc = 0;
            char *saveptr2;
            char *tok = strtok_r(seg, " ", &saveptr2);
            while (tok) {
                cmds[ncmd].args[cmds[ncmd].argc++] = tok;
                tok = strtok_r(NULL, " ", &saveptr2);
                if (cmds[ncmd].argc >= AARG_MAX-1) {
                    fprintf(stderr, "Error: too many process arguments\n");
                    error = 1;
                    break;
                }
            }
            if (error) break;
            cmds[ncmd].args[cmds[ncmd].argc] = NULL;
            if (cmds[ncmd].argc == 0) {
                fprintf(stderr, "Error: missing command\n");
                error = 1;
                break;
            }
            ncmd++;
            seg = strtok_r(NULL, "|", &saveptr1);
        }

        if(error) continue;

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

        if (input_file) {
            int fd = open(input_file, O_RDONLY);
            if (fd < 0) { fprintf(stderr, "Error: cannot open input file\n"); continue; }
            close(fd);
        }
        if (output_file) {
            int fd = open(output_file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
            if (fd < 0) { fprintf(stderr, "Error: cannot open output file\n"); continue; }
            close(fd);
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
                    dup2(fd,STDIN_FILENO); 
                    close(fd);
                }
                if (i==ncmd-1 && output_file) {
                    int fd=open(output_file,O_WRONLY|O_CREAT|O_TRUNC,0644);
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
