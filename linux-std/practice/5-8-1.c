#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* 本章で作ったcatコマンドを改造して、コマンドライン引数でファイル名が渡されなかったら標準入力を読むようにしなさい。 */


static void do_cat(const char *path);
static void die(const char *s);
static void std_cat();


int main(int argc, char *argv[])
{
    int i;
    if (argc < 2) {
        std_cat();
    }

    for (i = 1; i < argc; i++) {
        do_cat(argv[i]);
    }
    exit(0);
}

#define BUFFER_SIZE 2048

static void do_cat(const char *path)
{
    int fd;
    unsigned char buf[BUFFER_SIZE];
    int n;

    // O_RDONLY：読み込み専用
    fd = open(path, O_RDONLY);
    if (fd < 0) die(path);
    for (;;) {
        n = read(fd, buf, sizeof buf);
        if (n < 0) die(path); // エラーが起きたとき
        if (n == 0) break; // ファイル終端に達したとき
        // STDOUT_FILENO: 標準出力
        // readで読み込んだバイト数だけ書き込む
        if (write(STDOUT_FILENO, buf, n) < 0) die(path);
    }
    if (close(fd) < 0) die(path); // エラーが起きたとき
}

// 標準入力から読みこんでcatを実行するコマンド
static void std_cat()
{
    int n;
    unsigned char buf[BUFFER_SIZE];

    for (;;) {
        n = read(STDIN_FILENO, buf, sizeof buf);
        if (n < 0) die("STDIN_FILENO");
        if (n == 0) break; // ここがどうなるか

        if (write(STDERR_FILENO, buf, n) < 0) die("STDIN_FILENO");
    }
}

static void die(const char *s)
{
    perror(s);
    exit(1);
}