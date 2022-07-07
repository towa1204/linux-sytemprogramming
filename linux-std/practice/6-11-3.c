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
    FILE *f;
    unsigned char buf[BUFFER_SIZE];

    f = fopen(path, "r");
    if (!f) die(path);

    for (;;) {
        size_t n_read = fread(buf, 1, sizeof buf, f);
        if (ferror(f)) die(path); // エラーが起きたとき
        size_t n_written = fwrite(buf, 1, n_read, stdout);
        if (n_written < n_read) die(path);
        if (n_read < sizeof buf) break; // ファイル終端に達したとき
    }
    if (fclose(f) != 0) die(path);
}

static void die(const char *s)
{
    perror(s);
    exit(1);
}