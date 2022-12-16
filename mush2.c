#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <mush.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>

/* perms for all new files with RW */
#define RWPERMS S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH
#define WRITEFLAG O_WRONLY | O_CREAT | O_TRUNC
#define READ_END 0
#define WRITE_END 1

/* Options for verbosity */
typedef struct Options {
    int opt_verbose;
    int opt_parseonly;
    char *opt_mode;
} Options;

char *parse_command(pipeline *pipeline, FILE *infile, Options *opt);
void run_mush2(Options *opt, FILE *in);
void print_options(Options *opt);
void handler();

Options options = {0, 0, "(null)"};
static int sigint_check = 0;

int main(int argc, char *argv[]) {

    int opt, opt_pos;
    FILE *cmd_file;
    struct sigaction sa;

    sa.sa_handler = handler; /* set handler function of pointer */
    sigemptyset(&sa.sa_mask); /* empty out the signal mask */
    sa.sa_flags = 0; /* have no flags to handle */

    /* create the sigaction handler fro SIGALRM*/
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    if ((opt = getopt(argc, argv, "v")) != -1) {
        options.opt_verbose++;
    }

    if ((argc == 1) || (opt != -1 && (argc - optind) == 0)) {
        /* run from tty or terminal if you didn't specify a filename */
        if (options.opt_verbose)
            print_options(&options);
        run_mush2(&options, stdin);
    }
    /* read from the command line of the rest of the files listed */
    else if ((argc == 2) || (opt != -1 && (optind == 3 || optind == 2))) {
        /* run the commands from infile */
        if (options.opt_verbose)    
            print_options(&options);
        
        /* get the position of the file you want to open in command line */
        if (optind == 3 && opt != -1)
            opt_pos = 1;
        else if (optind == 2 && opt != -1) 
            opt_pos = 2;
        else
            opt_pos = 1;

        /* open the file specified and pass the file pointer to the run_mush2()
         * function. */
        if (!(cmd_file = fopen(argv[opt_pos], "r"))) {
            perror(argv[optind]);
            exit(EXIT_FAILURE);
        }
        /* run mush2 with the comands from cmd_file */
        run_mush2(&options, cmd_file);
        /* close both files opened */
        if (fclose(cmd_file) == -1) {
            perror("fclose");
            exit(EXIT_FAILURE);
        }
    }
    else {
        fprintf(stderr, "usage: ./mush2 [ -v ] [ infile ]\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}

void run_mush2(Options *opt, FILE *in) {
    /* start running mush2 LETS GOOOO */

    pipeline pipeline; /* arguments of pipeline. structure from mush.h (Nico) */
    char *parsedLine; /* used for storing the character pointer to free */
    int old[2]; /* old pipe */
    int next[2]; /* next pipe to new process to exec() */
    int i, num_cmds, status, term_child, in_fd, pipe_amount;
    struct clstage stage;
    pid_t child;
    sigset_t sigintset;

    if (sigemptyset(&sigintset) == -1) {
        perror("sigemptyset");
        exit(EXIT_FAILURE);
    }
    if (sigaddset(&sigintset, SIGINT) == -1) {
        perror("sigaddset");
        exit(EXIT_FAILURE);
    }
    /* Go through once. Check if it's feof or ^D or not */
    do {
        sigint_check = 0;
        /* check if it's tty or terminal and also if the FILE pointer is 
         * equal to the stdin pointer; then print out the prompt */
        if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && in == stdin) 
            printf("8-P "); /* mush2 prompt this first in stdin */

        /* parse the command line and return the character pointer to the 
         * command line so it's able to be freed later. */
        parsedLine = parse_command(&pipeline, in, opt);
        /* if parsedLine is not NULL, meaning no error occured, then run 
         * the commands, else skip the command. */
        if (parsedLine != NULL) {
            /* run the first command (parent process)*/
            num_cmds = pipeline -> length;
            pipe_amount = num_cmds - 1;
            /* Block sigint during pipe process. This will be unblocked before
             * the process exec() */
            if (sigprocmask(SIG_BLOCK, &sigintset, NULL) == -1) {
                perror("sigprocmask block");
                exit(EXIT_FAILURE);
            }
            
            if (pipe(old)) {
                perror("old pipe");
                exit(EXIT_FAILURE);
            }

            /* start the running processes and the piping piper plumber */
            for (i = 0; i < num_cmds; i++) {
                stage = (pipeline -> stage)[i];

                /* run cd in parent only */
                if (!strcmp(stage.argv[0], "cd")) {
                    if (stage.argc == 1) {
                        /* chdir into HOME directory */
                        if (chdir(getenv("HOME")) == -1) {
                            perror(stage.argv[1]);
                            close(old[READ_END]);
                            close(old[WRITE_END]);
                            break;
                        }
                    }
                    else if (stage.argc > 2) {
                        fprintf(stderr, "usage: cd [ destdir ]\n");
                        break;
                    }
                    else if (stage.argc == 2 && !strcmp(stage.argv[1], "~")) {
                        /* chdir into HOME directory */
                        if (chdir(getenv("HOME")) == -1) {
                            perror(stage.argv[1]);
                            close(old[READ_END]);
                            close(old[WRITE_END]);
                            break;
                        }
                    }
                    else if (chdir(stage.argv[1]) == -1) {
                        perror(stage.argv[1]);
                        close(old[READ_END]);
                        close(old[WRITE_END]);
                        break;
                    }
                    close(old[READ_END]);
                    close(old[WRITE_END]);
                    break;
                }
                /* run pipeline -> length amount of program, but 
                 * only create pipeline -> length - 1 amount of pipe */
                if (i < pipe_amount) {
                    if (pipe(next)) {
                        perror("next pipe");
                        exit(EXIT_FAILURE);
                    }
                }
    
                /* fork your child */
                if ((child = fork()) == -1) {
                    /* error of fork */
                    perror("fork child");
                    exit(EXIT_FAILURE);
                }
                else if (!child) {

                    /* child process 
                     * NOTE: child process will execvp() and then exit. So 
                     * it doesn't matter what the instruction after this if
                     * statement are. */

                    /* if the inname is not null, then open the file and 
                     * redirect to stdin */
                    if (stage.inname) {
                        if ((in_fd = open(stage.inname, O_RDONLY)) == -1) {
                            perror("open open read");
                            exit(EXIT_FAILURE);
                        }
                        if (dup2(in_fd, STDIN_FILENO) == -1) {
                            perror("dup2 in_fd");
                            exit(EXIT_FAILURE);
                        }
                        if (close(in_fd) == -1) {
                            perror("close in_fd");
                            exit(EXIT_FAILURE);
                        }
                    }
                    /* Redirect the stdout to the filename if there is 
                     * a redirect. Will truncate the file to 0 if there are 
                     * anything inside. If the file is created, it will have 
                     * rw permissions for everyone (UGO). */
                    if (stage.outname) {
                        if ((in_fd = open(stage.outname, WRITEFLAG, 
                                RWPERMS)) == -1){
                            perror("open write");
                            exit(EXIT_FAILURE);
                        }
                        if (dup2(in_fd, STDOUT_FILENO) == -1) {
                            perror("dup2 in_fd");
                            exit(EXIT_FAILURE);
                        }
                        if (close(in_fd) == -1) {
                            perror("close in_fd");
                            exit(EXIT_FAILURE);
                        }
                    }

                    /* This pipe would connect the current process's/command's
                     * input stdout to the next command's stdin */

                    /* redirect the old read to stdin for the next child process 
                     * to execute and read from stdin */
                    /* PS: I don't think this line of code matter if it's first 
                     * command since you wouldn't read from the read pipe. so 
                     * I think it should be > 1*/
                    if (pipe_amount > 0) {
                        /* if this is not the first child process, then dup2 */
                        if (dup2(old[READ_END], STDIN_FILENO) == -1) {
                            perror("dup2 old failed");
                            exit(EXIT_FAILURE);
                        }
                    }
                    if (i < pipe_amount) {
                        /* if this is not the last child process, then dup2 */
                        if (dup2(next[WRITE_END], STDOUT_FILENO) == -1) {
                            perror("dup2 next failed");
                            exit(EXIT_FAILURE);
                        }
                    }


                    /* clean up all the pipes and then execute the commands */
                    if (close(old[0]) == -1) {
                        perror("close old read in child");
                        exit(EXIT_FAILURE);
                    }
                    if (close(old[1]) == -1) {
                        perror("close old write in child");
                        exit(EXIT_FAILURE);
                    }
                    if (i < pipe_amount) {
                        if (close(next[0]) == -1) {
                            perror("close next read in child");
                            exit(EXIT_FAILURE);
                        }
                        if (close(next[1]) == -1) {
                            perror("close next write in child");
                            exit(EXIT_FAILURE);
                        }
                    }

                    /* unblock SIGINT before exec */
                    if (sigprocmask(SIG_UNBLOCK, &sigintset, NULL) == -1) {
                        perror("sigprocmask unblock");
                        exit(EXIT_FAILURE);
                    }

                    /* execute commands */
                    if (execvp(stage.argv[0], stage.argv) == -1) {
                        perror(stage.argv[0]);
                        exit(EXIT_FAILURE);
                    }
                    /* if it returns, something went bad so exit failure */
                    exit(EXIT_FAILURE);
                }
                /* parent process */
                /* clean up all of the pipes that are not needed by closing 
                 * them and reassigning the old one to the new pipes to 
                 * be fork by child and used */
                if (close(old[READ_END]) == -1) {
                    perror("close old read");
                    exit(EXIT_FAILURE);
                }
                if (close(old[WRITE_END]) == -1) {
                    perror("close old write");
                    exit(EXIT_FAILURE);
                }
                /* set your old to next for next process */
                old[READ_END] = next[READ_END];
                old[WRITE_END] = next[WRITE_END];
            }
            
            /* if there are children, then wait for their process to finish 
             * and check for the termination status */
            while (strcmp(stage.argv[0], "cd") && (num_cmds)--) {
                /* parent since child pid would be non-zero */
                if ((term_child = wait(&status)) == -1) {
                    if (!sigint_check) {
                        perror("wait");
                        exit(EXIT_FAILURE);
                    }
                    num_cmds++;
                }
                /* if the return status was normal, print succeed else process 
                * had an error. */
                /* if ((!WIFEXITED(status) || WEXITSTATUS(status))) {
                    printf("Process %d exited with an error value.\n", 
                        term_child);
                } */
            }

            free(parsedLine);
            lineno++; /* increment the shell command line counter */
        }
        /* free parsedLine and pipeline */
        if (!feof(in) && !sigint_check)
            free_pipeline(pipeline);

        if (sigint_check) {
            clearerr(in);
        }

        /* Stop valgrind from complaining about memory from the 
         * mush.h library */
        yylex_destroy();
        
    } while((!feof(in) && !ferror(in)) || sigint_check); 
}

char *parse_command(pipeline *pipeline, FILE *infile, Options *opt) {
    /* parses the command line and fill up parser and pipeline using 
     * the functions given by mush2 library provided by Nico */
    char *parsedLine;
    parsedLine = readLongString(infile);
    /* Error Check: if crack_pipeline returns a NULL pointer, then error out 
     * Stored the new pipeline into the pipeline buffer in the parameter. */
    if (parsedLine != NULL) {
        if (!((*pipeline = crack_pipeline(parsedLine)))) {
            /* something about error checking of crack_pipeline */
            if ( clerror != E_EMPTY && clerror != E_BADIN &&
                 clerror != E_BADOUT && clerror != E_BADSTR) {
                    ;
            }
            free(parsedLine); /* free parsedLine and return NULL */
            parsedLine = NULL;
        }
        if (opt -> opt_verbose && *pipeline != NULL) 
            print_pipeline(stdout, *pipeline);
    }

    return parsedLine;
}

void print_options(Options *opt) {
    printf("Options:\n");
    printf("  int   opt_verbose   = %d\n", opt -> opt_verbose);
    printf("  int   opt_parseonly = %d\n", opt -> opt_parseonly);
    printf("  char *opt_mode      = %s\n", opt -> opt_mode);
}

void handler() {
    /* probably empty */
    sigint_check = 1;
}
