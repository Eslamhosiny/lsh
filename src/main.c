/*
 * lsh - Little Shell
 *
 * Original by Stephen Brennan:
 * https://brennan.io/2015/01/16/write-a-shell-in-c/
 *
 * Extended with additional builtins:
 *   pwd, echo, history, env
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* ─────────────────────────────────────────────
   HISTORY
   ───────────────────────────────────────────── */
#define HISTORY_MAX 100
static char *history[HISTORY_MAX];
static int   history_count = 0;

static void history_add(const char *line)
{
    if (history_count < HISTORY_MAX) {
        history[history_count++] = strdup(line);
    } else {
        free(history[0]);
        memmove(history, history + 1,
                (HISTORY_MAX - 1) * sizeof(char *));
        history[HISTORY_MAX - 1] = strdup(line);
    }
}

static void history_free(void)
{
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
        history[i] = NULL;
    }
    history_count = 0;
}

/* ─────────────────────────────────────────────
   TOKENIZER / LINE READER
   ───────────────────────────────────────────── */
#define LSH_RL_BUFSIZE  1024
#define LSH_TOK_BUFSIZE  64
#define LSH_TOK_DELIM   " \t\r\n\a"

char *lsh_read_line(void)
{
    char *line = NULL;
    size_t bufsize = 0;

    if (getline(&line, &bufsize, stdin) == -1) {
        if (feof(stdin)) {
            printf("\n");
            exit(EXIT_SUCCESS);
        } else {
            perror("lsh: getline");
            exit(EXIT_FAILURE);
        }
    }
    return line;
}

char **lsh_split_line(char *line)
{
    int    bufsize = LSH_TOK_BUFSIZE;
    int    position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char  *token;

    if (!tokens) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, LSH_TOK_DELIM);
    while (token != NULL) {
        tokens[position++] = token;

        if (position >= bufsize) {
            bufsize += LSH_TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
        token = strtok(NULL, LSH_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

/* ─────────────────────────────────────────────
   FORWARD DECLARATIONS FOR BUILTINS
   ───────────────────────────────────────────── */
int lsh_cd(char **args);
int lsh_help(char **args);
int lsh_exit(char **args);
int lsh_pwd(char **args);
int lsh_echo(char **args);
int lsh_history(char **args);
int lsh_env(char **args);

/* ─────────────────────────────────────────────
   BUILTIN COMMAND TABLE
   ───────────────────────────────────────────── */
char *builtin_str[] = {
    "cd",
    "help",
    "exit",
    "pwd",
    "echo",
    "history",
    "env"
};

int (*builtin_func[])(char **) = {
    &lsh_cd,
    &lsh_help,
    &lsh_exit,
    &lsh_pwd,
    &lsh_echo,
    &lsh_history,
    &lsh_env
};

int lsh_num_builtins(void)
{
    return sizeof(builtin_str) / sizeof(char *);
}

/* ─────────────────────────────────────────────
   ORIGINAL BUILTINS (cd / help / exit)
   ───────────────────────────────────────────── */
int lsh_cd(char **args)
{
    if (args[1] == NULL) {
        fprintf(stderr, "lsh: cd: expected argument\n");
    } else {
        if (chdir(args[1]) != 0)
            perror("lsh: cd");
    }
    return 1;
}

int lsh_help(char **args)
{
    (void)args;
    printf("========================================\n");
    printf("  lsh - Little Shell  (extended edition)\n");
    printf("========================================\n");
    printf("Built-in commands:\n\n");
    for (int i = 0; i < lsh_num_builtins(); i++)
        printf("  %s\n", builtin_str[i]);
    printf("\nFor external programs, type the program name and arguments.\n");
    printf("Use man <program> for documentation on external programs.\n");
    return 1;
}

int lsh_exit(char **args)
{
    (void)args;
    history_free();
    return 0;
}

/* ─────────────────────────────────────────────
   NEW BUILTIN: pwd
   ───────────────────────────────────────────── */
int lsh_pwd(char **args)
{
    (void)args;
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("lsh: pwd");
    } else {
        printf("%s\n", cwd);
    }
    return 1;
}

/* ─────────────────────────────────────────────
   NEW BUILTIN: echo
   ───────────────────────────────────────────── */
int lsh_echo(char **args)
{
    for (int i = 1; args[i] != NULL; i++) {
        printf("%s", args[i]);
        if (args[i + 1] != NULL)
            printf(" ");
    }
    printf("\n");
    return 1;
}

/* ─────────────────────────────────────────────
   NEW BUILTIN: history
   ───────────────────────────────────────────── */
int lsh_history(char **args)
{
    (void)args;
    if (history_count == 0) {
        printf("lsh: history: no commands recorded yet\n");
    } else {
        for (int i = 0; i < history_count; i++)
            printf("  %3d  %s\n", i + 1, history[i]);
    }
    return 1;
}

/* ─────────────────────────────────────────────
   NEW BUILTIN: env
   ───────────────────────────────────────────── */
extern char **environ;

int lsh_env(char **args)
{
    (void)args;
    for (char **env = environ; *env != NULL; env++)
        printf("%s\n", *env);
    return 1;
}

/* ─────────────────────────────────────────────
   LAUNCH: fork + execvp
   ───────────────────────────────────────────── */
int lsh_launch(char **args)
{
    pid_t pid, wpid;
    int   status;

    pid = fork();

    if (pid == 0) {
        if (execvp(args[0], args) == -1)
            perror("lsh");
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("lsh: fork");
    } else {
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

/* ─────────────────────────────────────────────
   EXECUTE
   ───────────────────────────────────────────── */
int lsh_execute(char **args)
{
    if (args[0] == NULL)
        return 1;

    for (int i = 0; i < lsh_num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0)
            return (*builtin_func[i])(args);
    }

    return lsh_launch(args);
}

/* ─────────────────────────────────────────────
   MAIN LOOP
   ───────────────────────────────────────────── */
void lsh_loop(void)
{
    char  *line;
    char **args;
    int    status;

    do {
        printf("lsh> ");
        fflush(stdout);

        line = lsh_read_line();

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        if (line[0] != '\0')
            history_add(line);

        args   = lsh_split_line(line);
        status = lsh_execute(args);

        free(line);
        free(args);

    } while (status);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("lsh - Little Shell  |  type 'help' for commands\n");
    lsh_loop();
    return EXIT_SUCCESS;
}
