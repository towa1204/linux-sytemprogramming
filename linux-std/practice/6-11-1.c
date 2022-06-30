#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    int i;

    for (i = 1; i < argc; i++) {
        FILE *f;
        int c;

        f = fopen(argv[i], "r");
        if (!f) {
            perror(argv[i]);
            exit(1);
        }

        // fgetc: 1バイトずつ読み込む
        while ((c = fgetc(f)) != EOF) {
            if (c == '\t') {
                if (putchar('\t') < 0) exit(1);
            } else if (c == '\n') {
                if (putchar('$') < 0) exit(1);
            }
            if (putchar(c) < 0) exit(1);
        }
        fclose(f);
    }
    exit(0);
}