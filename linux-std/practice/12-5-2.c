#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>

// fork()とexec()を使ってプログラムを起動する、簡単なシェルを書きなさい
// https://github.com/aamine/stdlinux2-source/blob/master/sh1.c

struct cmd {
    char **argv;
    long argc;      /* used length of argv */
    long capa;      /* allocated length of argv */
};

static void invoke_cmd(struct cmd *cmd);
static struct cmd* read_cmd(void);
static struct cmd* parse_cmd(char *cmdline);
static void free_cmd(struct cmd *p);
static void* xmalloc(size_t sz);
static void* xrealloc(void *ptr, size_t sz);

static char *program_name;

#define PROMPT "$ "

int main(int argc, char *argv[])
{
    program_name = argv[0];
    
    for (;;) {
        struct cmd* cmd;

        fprintf(stdout, PROMPT);
        fflush(stdout);

        cmd = read_cmd();

        /* デバッグ用 */
        // int i;
        // for (i = 0; i < cmd->argc; i++)
        //     printf("cmd: argv[%d] = %s\n", i, cmd->argv[i]);

        if (cmd->argc > 0)
            invoke_cmd(cmd);
        
        free_cmd(cmd);
    }
}

static void invoke_cmd(struct cmd *cmd)
{
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) {
        // 親プロセス
        waitpid(pid, NULL, 0);
    } else {
        // 子プロセス
        execvp(cmd->argv[0], cmd->argv);
        
        // エラー発生時
        fprintf(stderr, "%s command not found: %s\n", 
                        program_name, cmd->argv[0]);
        exit(1);
    }
}

#define LINE_BUF_SIZE 2048

static struct cmd* read_cmd(void)
{
    static char buf[LINE_BUF_SIZE];

    if (fgets(buf, LINE_BUF_SIZE, stdin) == NULL)
        exit(0); // Ctrl-D(EOF) のとき正常終了
    
    return parse_cmd(buf);
}

#define INIT_CAPA 16

static struct cmd* parse_cmd(char *cmdline)
{
    // 与えられた文字列をパースして*cmdに配置

    char *p = cmdline;
    struct cmd *cmd;

    // printf("cmdline = %s", cmdline);

    // xmalloc: mallocのラッパー, NULLチェックしなくていい
    // struct cmd型の値が入る分の領域を確保して、先頭のポインタを返す
    cmd = xmalloc(sizeof(struct cmd));
    cmd->argc = 0; // 0に初期化
    // argvはポインタ型なのでポインタが指すデータの範囲を確保して、その先頭ポインタを設定
    // 文字列を指すINIT_CAPA個のポインタ分確保
    cmd->argv = xmalloc(sizeof(char *) * INIT_CAPA);
    cmd->capa = INIT_CAPA;

    while (*p) {
        // printf("*p = %c", *p);
        // isspace: タブ文字なども含む空白文字であるか
        while (*p && isspace((int)*p)) {
            *p++ = '\0'; // 空白文字をNULLにして次の文字へ
        }
        if (*p) {
            if (cmd->capa <= cmd->argc + 1) {
                // 入力文字列を空白文字で区切って得られた各文字列の個数が確保していたサイズを超えるとき
                // 追加で確保する
                cmd->capa *= 2;
                cmd->argv = xrealloc(cmd->argv, cmd->capa);
            }
            cmd->argv[cmd->argc] = p; // 入力文字列を空白文字で区切って得られた各文字列の先頭アドレスを代入
            cmd->argc++; // // 入力文字列を空白文字で区切って得られた各文字列の個数のカウント
        }
        while (*p && !isspace((int)*p)) {
            // 空白文字が現れるまでスキップ
            p++;
        }
    }
    cmd->argv[cmd->argc] = NULL;
    return cmd;
}

static void free_cmd(struct cmd *cmd)
{
    free(cmd->argv);
    free(cmd);
}

static void* xmalloc(size_t size)
{
    void *p;

    p = malloc(size);
    if (!p) {
        perror("malloc");
        exit(1);
    }
    return p;
}

static void* xrealloc(void *ptr, size_t size)
{
    void *p;

    if (!ptr) return xmalloc(size);
    p = realloc(ptr, size);
    if (!p) {
        perror("realloc");
        exit(1);
    }
    return p;
}