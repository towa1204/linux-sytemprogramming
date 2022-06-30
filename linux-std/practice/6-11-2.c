#include <stdio.h>
#include <stdlib.h>

void do_word_count(const char *file_path);

int main (int argc, char *argv[])
{
    int i;
    if (argc < 2) {
        fprintf(stderr, "%s: file name not given\n", argv[0]);
    }

    for (i = 1; i < argc; i++) {
        do_word_count(argv[i]);
    }
    exit(0);
}

void do_word_count(const char *file_path)
{
    FILE *f;
    f = fopen(file_path, "r");
    if (!f) {
        perror(file_path);
        exit(1);
    }

    int c;
    unsigned long cnt = 0;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') cnt++;
    }

    printf("%lu\n", cnt);

    fclose(f);
}