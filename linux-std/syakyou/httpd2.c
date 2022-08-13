/*
    httpd2.c -- standalone HTTP 1.0 server
    
    Copyright (c) 2004,2005,2017 Minero Aoki
    This program is free software.
    Redistribution and use in source and binary forms,
    with or without modification, are permitted.
*/

// https://github.com/aamine/stdlinux2-source/blob/master/httpd2.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <syslog.h>
#define _GNU_SOURCE
#include <getopt.h>

/****** Data Type Definitions ********************************************/

// リンクリスト
// HTTPヘッダの例： User-Agent, Set-Cookie
struct HTTPHeaderField {
    char *name;
    char *value;
    struct HTTPHeaderField *next;
};

struct HTTPRequest {
    int protocol_minor_version; // 例: HTTP1.1なら1
    char *method; // 例: GET, HEAD
    char *path; // 例: /example.html
    struct HTTPHeaderField *header;
    char *body; // エンティティボディ
    long length; // ボディの長さ
};

struct FileInfo {
    char *path; // ファイルシステム上のファイルの絶対パス
    long size; // ファイルのサイズ(バイト単位)
    int ok; // ファイルが存在するなら非ゼロ
};

/****** Constants ********************************************************/

#define SERVER_NAME "LittleHTTP"
#define SERVER_VERSION "1.0"
#define HTTP_MINOR_VERSION 0
#define BLOCK_BUF_SIZE 1024
#define LINE_BUF_SIZE 4096
#define MAX_REQUEST_BODY_LENGTH (1024 * 1024)
#define MAX_BACKLOG 5
#define DEFAULT_PORT "80"

/****** Function Prototypes **********************************************/

static void setup_environment(char *root, char *user, char *group);
typedef void (*sighandler_t)(int);
static void install_signal_handlers(void);
static void trap_signal(int sig, sighandler_t handler);
static void detach_children(void);
static void signal_exit(int sig);
static void noop_handler(int sig);
static void become_daemon(void);
static int listen_socket(char *port);
static void server_main(int server, char *docroot);
static void service(FILE *in, FILE *out, char *docroot);
static struct HTTPRequest* read_request(FILE *in);
static void read_request_line(struct HTTPRequest *req, FILE *in);
static struct HTTPHeaderField* read_header_field(FILE *in);
static void upcase(char *str);
static void free_request(struct HTTPRequest *req);
static long content_length(struct HTTPRequest *req);
static char* lookup_header_field_value(struct HTTPRequest *req, char *name);
static void respond_to(struct HTTPRequest *req, FILE *out, char *docroot);
static void do_file_response(struct HTTPRequest *req, FILE *out, char *docroot);
static void method_not_allowed(struct HTTPRequest *req, FILE *out);
static void not_implemented(struct HTTPRequest *req, FILE *out);
static void not_found(struct HTTPRequest *req, FILE *out);
static void output_common_header_fields(struct HTTPRequest *req, FILE *out, char *status);
static struct FileInfo* get_fileinfo(char *docroot, char *path);
static char* build_fspath(char *docroot, char *path);
static void free_fileinfo(struct FileInfo *info);
static char* guess_content_type(struct FileInfo *info);
static void* xmalloc(size_t sz);
static void log_exit(const char *fmt, ...);

/****** Functions ********************************************************/

#define USAGE "Usage: %s [--port=n] [--chroot --user=u --group=g] [--debug] <docroot>\n"

static int debug_mode = 0;

static struct option longopts[] = {
    {"debug",  no_argument,       &debug_mode, 1},
    {"chroot", no_argument,       NULL, 'c'},
    {"user",   required_argument, NULL, 'u'},
    {"group",  required_argument, NULL, 'g'},
    {"port",   required_argument, NULL, 'p'},
    {"help",   no_argument,       NULL, 'h'},
    {0, 0, 0, 0}
};

int main(int argc, char *argv[])
{
    int server_fd;
    char *port = NULL;
    char *docroot;
    int do_chroot = 0;
    char *user = NULL;
    char *group = NULL;
    int opt;

    while ((opt = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
        switch (opt) {
        case 0:
            break;
        case 'c':
            do_chroot = 1;
            break;
        case 'u':
            user = optarg;
            break;
        case 'g':
            group = optarg;
            break;
        case 'p':
            port = optarg;
            break;
        case 'h':
            fprintf(stdout, USAGE, argv[0]);
            exit(0);
        case '?':
            fprintf(stderr, USAGE, argv[0]);
            exit(1);
        }
    }
    if (optind != argc - 1) {
        fprintf(stderr, USAGE, argv[0]);
        exit(1);
    }
    docroot = argv[optind];

    if (do_chroot) {
        setup_environment(docroot, user, group);
        docroot = "";
    }
    install_signal_handlers();
    server_fd = listen_socket(port);
    if (!debug_mode) {
        openlog(SERVER_NAME, LOG_PID|LOG_NDELAY, LOG_DAEMON);
        become_daemon();
    }
    server_main(server_fd, docroot);
    exit(0);
}

static void server_main(int server_fd, char *docroot)
{
    for (;;) {
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof addr;
        int sock;
        int pid;

        /* この関数は、接続待ちソケット socket 宛ての保留状態の接続要求が入っているキューから
           先頭の接続要求を取り出し、接続済みソケットを新規に生成し、 
           そのソケットを参照する新しいファイルディスクリプターを返す。 */
        sock = accept(server_fd, (struct sockaddr*)&addr, &addrlen); // 
        if (sock < 0) log_exit("accept(2) failed: %s", strerror(errno));
        
        pid = fork();
        if (pid < 0) exit(3); // fork失敗時
        if (pid == 0) {
            // 子プロセス
            FILE *inf = fdopen(sock, "r");
            FILE *outf = fdopen(sock, "w");

            service(inf, outf, docroot);
            exit(0);
        }
        close(sock); // 親プロセスと結びついたままの接続済みソケットをclose 図17.2
    }
}

static int listen_socket(char *port)
{
    struct addrinfo hints, *res, *ai;
    int err;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE; // ソケットをサーバ用として使う

    // サーバ側のときはプロセスが動作しているそのホストのアドレス構造体をgetaddrinfo()で得る
    if ((err = getaddrinfo(NULL, port, &hints, &res)) != 0)
        log_exit(gai_strerror(err));

    // hintsに当てはまるアドレス構造体のリストresの要素に対してsocket, bind, listenして、成功した最初のアドレスを使う
    for (ai = res; ai; ai = ai->ai_next) {
        int sock;

        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol); // 通信のためのエンドポイントを作成
        if (sock < 0) continue;
        if (bind(sock, ai->ai_addr, ai->ai_addrlen) < 0) { // ソケットに名前をつける
            close(sock);
            continue;
        }
        // BACKLOGは、保留中の接続のキューの最大長
        if (listen(sock, MAX_BACKLOG) < 0) { // ソケット上の接続を待つ
            close(sock);
            continue;
        }
        freeaddrinfo(res);
        return sock;
    }
    log_exit("failed to listen socket");
    return -1; /* NOT REACH */
}

static void become_daemon(void)
{
    int n;

    // ルートディレクトリに移動
    if (chdir("/") < 0)
        log_exit("chdir(2) failed: %s", strerror(errno));
    
    // 標準入出力を/dev/nullにつなぐ
    // freopen: path で指定された名前のファイルを開き、ストリームと結びつける
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);

    n = fork();
    if (n < 0) log_exit("fork(2) failed: %s", strerror(errno));
    if (n != 0) _exit(0); /* 親プロセスは終了する */
    if (setsid() < 0) log_exit("setsid(2) failed: %s", strerror(errno));
}

// rootに与えられたディレクトリをルートディレクトリとして設定
static void setup_environment(char *root, char *user, char *group)
{
    struct passwd *pw;
    struct group *gr;

    if (!user || !group) {
        fprintf(stderr, "use both of --user and --group\n");
        exit(1);
    }
    gr = getgrnam(group);
    if (!gr) {
        fprintf(stderr, "no such group: %s\n", group);
        exit(1);
    }
    // 自プロセスの実グループIDと実効グループIDをgr->gr_gidに変更
    if (setgid(gr->gr_gid) < 0) {
        perror("setgid(2)");
        exit(1);
    }
    // userが所属しているグループを自プロセスに設定, gr->gr_gidに設定
    if (initgroups(user, gr->gr_gid) < 0) {
        perror("initgroups(2)");
        exit(1);
    }
    pw = getpwnam(user);
    if (!pw) {
        fprintf(stderr, "no such user: %s\n", user);
        exit(1);
    }
    
    // 呼び出し元プロセスのルートディレクトリを root で指定されたディレクトリに変更
    chroot(root);

    // 自プロセスの実グループIDと実効グループIDをpw->pw_uidに変更
    if (setuid(pw->pw_uid) < 0) {
        perror("setuid(2)");
        exit(1);
    }
}

static void service(FILE *in, FILE *out, char *docroot)
{
    struct HTTPRequest *req;

    req = read_request(in);
    respond_to(req, out, docroot);
    free_request(req);
}

static struct HTTPRequest* read_request(FILE *in)
{
    struct HTTPRequest *req;
    struct HTTPHeaderField *h;

    req = xmalloc(sizeof(struct HTTPRequest));
    // リクエストラインのパース reqに書き込む
    read_request_line(req, in);
    
    req->header = NULL;
    while (h = read_header_field(in)) {
        h->next = req->header;
        req->header = h;
    }
    
    req->length = content_length(req);
    if (req->length != 0) {
        if (req->length > MAX_REQUEST_BODY_LENGTH)
            log_exit("request body too long");
        req->body = xmalloc(req->length);
        if (fread(req->body, req->length, 1, in) < 1)
            log_exit("failed to read request body");
    } else {
        req->body = NULL;
    }
    return req;
}

static void read_request_line(struct HTTPRequest *req, FILE *in)
{
    char buf[LINE_BUF_SIZE];
    char *path, *p;

    // 1行読み込み 最大サイズはLINE_BUF_SIZE
    if (!fgets(buf, LINE_BUF_SIZE, in))
        log_exit("no request line");
    
    // 先頭から' 'を探してその先頭ポインタを返す, なければNULL
    p = strchr(buf, ' '); /* p (1) */
    if (!p) log_exit("parse error on request line (1): %s", buf);
    *p++ = '\0'; // ' 'を'\0'に置換して次のポインタへ
    req->method = xmalloc(p - buf); // メソッドの文字数分の領域を確保
    strcpy(req->method, buf);
    upcase(req->method); // メソッド名を大文字に変換

    path = p;
    p = strchr(path, ' ');  /* p (2) */
    if (!p) log_exit("parse error on request line(2): %s", buf);
    *p++ = '\0';
    req->path = xmalloc(p - path);
    strcpy(req->path, path);

    // strncasecmp: アルファベットの大文字小文字の区別を無視してstr1とstr2を比較
    if (strncasecmp(p, "HTTP/1.", strlen("HTTP/1.") != 0))
        log_exit("parse error request line (3): %s", buf);
    p += strlen("HTTP/1."); /* p (3) */
    req->protocol_minor_version = atoi(p);
}

static struct HTTPHeaderField* read_header_field(FILE *in)
{
    struct HTTPHeaderField *h;
    char buf[LINE_BUF_SIZE];
    char *p;

    // 1行読み込み 最大サイズはLINE_BUF_SIZE
    if (!fgets(buf, LINE_BUF_SIZE, in))
        log_exit("failed to read request header field: %s", strerror(errno));
    if ((buf[0] == '\n') || (strcmp(buf, "\r\n") == 0))
        return NULL; // 空行だった場合

    p = strchr(buf, ':'); // name:value の :
    if (!p) log_exit("parse error on request header field: %s", buf);
    *p++ = '\0';
    h = xmalloc(sizeof(struct HTTPHeaderField));
    h->name = xmalloc(p - buf);
    strcpy(h->name, buf); // name のセット

    // " \t"がpの先頭から何個あるか数えその長さを返す
    p += strspn(p, " \t"); // タブは飛ばす
    h->value = xmalloc(strlen(p) + 1); // strlen(p)はpを先頭ポインタとする文字列の長さ
    strcpy(h->value, p); // value のセット

    return h;
}

static long content_length(struct HTTPRequest *req)
{
    char *val;
    long len;

    val = lookup_header_field_value(req, "Content-Length");
    if (!val) return 0;
    len = atol(val);
    if (len < 0) log_exit("negative Content-Length value");
    return len;
}

static char* lookup_header_field_value(struct HTTPRequest *req, char *name)
{
    struct HTTPHeaderField *h;

    for (h = req->header; h; h = h->next) {
        if (strcasecmp(h->name, name) == 0)
            return h->value;
    }
    return NULL;
}

static struct FileInfo* get_fileinfo(char *docroot, char *urlpath)
{
    // ドキュメントルートとURLのパスからファイルシステム上のパスを生成
    struct FileInfo *info;
    struct stat st;

    info = xmalloc(sizeof(struct FileInfo));
    info->path = build_fspath(docroot, urlpath);
    info->ok = 0;

    // pathで表されるエントリの情報を取得しstに書きこむ
    if (lstat(info->path, &st) < 0) return info;  // 失敗時,okは0のままreturn
    if (!S_ISREG(st.st_mode)) return info; // 通常のファイルじゃない場合、okは0のままreturn

    info->ok = 1;
    info->size = st.st_size;
    return info;
}

// このままだと ../../のようなパスが渡されるとドキュメントルート外のファイルが見える
static char* build_fspath(char *docroot, char *urlpath)
{
    char *path;

    // 2回の+1は'/'の分と末尾の'\0'の分
    path = xmalloc(strlen(docroot) + 1 + strlen(urlpath) + 1);
    sprintf(path, "%s%s", docroot, urlpath); // docroot + urlpathをpathに書き込み
    return path;
}

static void free_fileinfo(struct FileInfo *info)
{
    free(info->path);
    free(info);
}

// HTTPリクエストreqに対するレスポンスをoutに書き込む
static void respond_to(struct HTTPRequest *req, FILE *out, char *docroot)
{
    if (strcmp(req->method, "GET") == 0)
        do_file_response(req, out, docroot);
    else if (strcmp(req->method, "HEAD") == 0)
        do_file_response(req, out, docroot);
    else if (strcmp(req->method, "POST") == 0)
        method_not_allowed(req, out);
    else
        not_implemented(req, out);
}

static void do_file_response(struct HTTPRequest *req, FILE *out, char *docroot)
{
    struct FileInfo *info;

    info = get_fileinfo(docroot, req->path);
    if (!info->ok) {
        free_fileinfo(info);
        not_found(req, out);
        return;
    }

    // レスポンスヘッダの出力
    output_common_header_fields(req, out, "200 OK");
    fprintf(out, "Content-Length: %ld\r\n", info->size);
    fprintf(out, "Content-Type: %s\r\n", guess_content_type(info));
    fprintf(out, "\r\n");

    if (strcmp(req->method, "HEAD") != 0) {
        // レスポンスバディの出力

        int fd;
        char buf[BLOCK_BUF_SIZE];
        ssize_t n;
        
        fd = open(info->path, O_RDONLY);
        if (fd < 0)
            log_exit("failed to open %s: %s", info->path, strerror(errno));
        for (;;) {
            // リクエストされたファイルの読み込み
            n = read(fd, buf, BLOCK_BUF_SIZE);
            if (n < 0)
                log_exit("failed to read %s: %s", info->path, strerror(errno));
            if (n == 0)
                break;
            // out にファイルの内容を書き込む
            if (fwrite(buf, 1, n, out) < n)
                log_exit("failed to write to socket");
        }
        close(fd);
    }
    fflush(out);
    free_fileinfo(info);
}

static void method_not_allowed(struct HTTPRequest *req, FILE *out)
{
    output_common_header_fields(req, out, "405 Method Not Allowed");
    fprintf(out, "Content-Type: text/html\r\n");
    fprintf(out, "\r\n");
    fprintf(out, "<html>\r\n");
    fprintf(out, "<header>\r\n");
    fprintf(out, "<title>405 Method Not Allowed</title>\r\n");
    fprintf(out, "<header>\r\n");
    fprintf(out, "<body>\r\n");
    fprintf(out, "<p>The request method %s is not allowed</p>\r\n", req->method);
    fprintf(out, "</body>\r\n");
    fprintf(out, "</html>\r\n");
    fflush(out);
}

static void not_implemented(struct HTTPRequest *req, FILE *out)
{
    output_common_header_fields(req, out, "501 Not Implemented");
    fprintf(out, "Content-Type: text/html\r\n");
    fprintf(out, "\r\n");
    fprintf(out, "<html>\r\n");
    fprintf(out, "<header>\r\n");
    fprintf(out, "<title>501 Not Implemented</title>\r\n");
    fprintf(out, "<header>\r\n");
    fprintf(out, "<body>\r\n");
    fprintf(out, "<p>The request method %s is not implemented</p>\r\n", req->method);
    fprintf(out, "</body>\r\n");
    fprintf(out, "</html>\r\n");
    fflush(out);
}

static void not_found(struct HTTPRequest *req, FILE *out)
{
    output_common_header_fields(req, out, "404 Not Found");
    fprintf(out, "Content-Type: text/html\r\n");
    fprintf(out, "\r\n");
    if (strcmp(req->method, "HEAD") != 0) {
        fprintf(out, "<html>\r\n");
        fprintf(out, "<header><title>Not Found</title><header>\r\n");
        fprintf(out, "<body><p>File not found</p></body>\r\n");
        fprintf(out, "</html>\r\n");
    }
    fflush(out);
}

#define TIME_BUF_SIZE 64
static void output_common_header_fields(struct HTTPRequest *req, FILE *out, char *status)
{
    time_t t;
    struct tm *tm;
    char buf[TIME_BUF_SIZE];

    t = time(NULL);
    tm = gmtime(&t);
    if (!tm) log_exit("gmtime() failed: %s", strerror(errno));
    strftime(buf, TIME_BUF_SIZE, "%a, %d %b %Y %H:%M:%S GMT", tm);
    fprintf(out, "HTTP/1.%d %s\r\n", HTTP_MINOR_VERSION, status);
    fprintf(out, "Date: %s\r\n", buf);
    fprintf(out, "Server: %s/%s\r\n", SERVER_NAME, SERVER_VERSION);
    fprintf(out, "Connection: close\r\n");
}

static void free_request(struct HTTPRequest *req)
{
    struct HTTPHeaderField *h, *head; // hは作業用変数

    head = req->header;

    // HTTPヘッダ領域のメモリ解放
    while (head) {
        h = head;
        head = head->next;
        free(h->name);
        free(h->value);
        free(h);
    }

    free(req->method);
    free(req->path);
    free(req->body);
    free(req);
}

// SIGPIPEを捕捉時、signal_exit関数を呼び出す(ログ出力して終了)
static void install_signal_handlers(void)
{
    trap_signal(SIGPIPE, signal_exit);
    detach_children();
}

static void trap_signal(int sig, sighandler_t handler)
{
    struct sigaction act;

    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART;
    if (sigaction(sig, &act, NULL) < 0)
        log_exit("sigaction() failed: %s", strerror(errno));
}

static void signal_exit(int sig)
{
    log_exit("exit by signal %d", sig);
}

static void detach_children(void)
{
    struct sigaction act;

    act.sa_handler = noop_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART | SA_NOCLDWAIT;
    if (sigaction(SIGCHLD, &act, NULL) < 0) {
        log_exit("sigaction() failed: %s", strerror(errno));
    }
}

static void noop_handler(int sig)
{
    ;
}

// strの文字列を大文字に変換
static void upcase(char *str)
{
    char *p;

    for (p = str; *p; p++) {
        *p = (char)toupper((int)*p);
    }
}

static char* guess_content_type(struct FileInfo *info)
{
    return "text/plain";   /* FIXME */
}

static void* xmalloc(size_t sz)
{
    void *p;

    p = malloc(sz);
    if (!p) log_exit("failed to allocate memory");
    return p;
}

// printf()と同じ形式の引数を受け付け、それをフォーマットしたものを標準エラー出力に出力し、exit()
static void log_exit(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (debug_mode) {
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
    } else {
        vsyslog(LOG_ERR, fmt, ap);
    }
    va_end(ap);
    exit(1);
}