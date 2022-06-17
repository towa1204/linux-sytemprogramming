#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static void do_cat(const char *path);
static void die(const char *s);

int main(int argc, char *argv[])
{
    int i;
    if (argc < 2) {
        fprintf(stderr, "%s: file name not given\n", argv[0]);
        exit(1);
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

static void die(const char *s)
{
    perror(s);
    exit(1);
}