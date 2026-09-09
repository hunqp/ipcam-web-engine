#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "main.h"
#include "helpers.h"

#define RECORDS_ROOT_DIR        (const char*)"/mnt/sdcard"
#define RECORDS_DECRYPT_TOOL    (const char*)"mp4-decrypt"

extern std::string stGetEnvirVariables(FCGX_Request &request, const char *name);

static void putHeader(FCGX_Request &m, const char *fmt, long long v) {
    char b[96];
    snprintf(b, sizeof(b), fmt, v);
    FCGX_PutStr(b, (int)strlen(b), m.out);     /* FCGX_FPrintF can't do %lld */
}

/* Decrypt `src` -> temp file -> open + unlink (anonymous) -> stream range -> close. */
static void decryptor(FCGX_Request &message, const char *filename) {
    char tmp[64] = {0};
    snprintf(tmp, sizeof(tmp), RAM_ROOT "/.%ld.mp4", (long)getpid());
    /* Remove previous temporary */
    unlink(tmp);

    int rc = runCommands("%s \"%s\" \"%s\" \"%s\"", RECORDS_DECRYPT_TOOL, APP_SECRET_UNIQUE_FILE, filename, tmp);
    if (rc == 0) {
        /* Return 0 mean SUCCESS decryptiton, so we can use `tmp` as filename. */
        filename = (const char*)tmp;
    }

    CGI_SYSD("[%d] Selected records: %s\r\n", rc, filename);

    struct stat st = {0};
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        HTTP_ResponseDataAsHTML(message, 500, "<h1>500</h1>");
        return;
    }
    fstat(fd, &st);

    bool partial = false;
    const long long total = (long long)st.st_size;
    long long a = 0, b = total > 0 ? total - 1 : 0;
    const std::string range = stGetEnvirVariables(message, "HTTP_RANGE");
    if (!range.empty()) {
        if (!HTTP_ExtractRangeHeader(range, total, a, b)) {
            close(fd);
            HTTP_ResponseRangeNotSatisfiable(message, total);
            return;
        }
        partial = !(a == 0 && b == total - 1);
    }

    FCGX_FPrintF(message.out, "Status: %s\r\n", partial ? "206 Partial Content" : "200 OK");
    FCGX_FPrintF(message.out, "Content-Type: video/mp4\r\n");
    FCGX_FPrintF(message.out, "Accept-Ranges: bytes\r\n");
    FCGX_FPrintF(message.out, "Cache-Control: no-store\r\n");
    if (partial) {
        char cr[96];
        snprintf(cr, sizeof(cr), "Content-Range: bytes %lld-%lld/%lld\r\n", a, b, total);
        FCGX_PutStr(cr, (int)strlen(cr), message.out);
    }
    putHeader(message, "Content-Length: %lld\r\n", b - a + 1);
    FCGX_FPrintF(message.out, "\r\n");

    if (lseek(fd, (off_t)a, SEEK_SET) == (off_t)a) {
        char buffer[128 * 1024];
        long long left = b - a + 1;
        while (left > 0) {
            size_t want = left < (long long)sizeof(buffer) ? (size_t)left : sizeof(buffer);
            ssize_t n = read(fd, buffer, want);
            if (n <= 0) {
                break;
            }
            if (FCGX_PutStr(buffer, (int)n, message.out) != n) {
                break;
            }
            left -= n;
        }
    }
    close(fd);

    /* Remove temporary file for reclaim RAM space */
    if (rc == 0) {
        unlink(tmp);
        CGI_SYSD("Remove records: %s\r\n", tmp);
    }
}

/*
    The path MUST-BE as format "/records/<yy-mm-dd>/[yy-mm-ddThh.mm.ss].mp4"
    So we need to resolve it into a canonical path under "/mnt/sdcard"
*/
void redirectFileRecords(FCGX_Request &message, const std::string &path) {
    std::string filename = path;
    filename.replace(0, std::string("/records").length(), RECORDS_ROOT_DIR);

    if (access(filename.c_str(), F_OK) != 0) {
        HTTP_ResponseDataAsHTML(message, 404, "<h1>404</h1>");
        return;
    }

    decryptor(message, filename.c_str());

    /* Plain clip: let lighttpd serve it (Content-Type/Length/Range all handled) */
    FCGX_FPrintF(message.out, "Status: 200 OK\r\n");
    FCGX_FPrintF(message.out, "Cache-Control: no-store\r\n");
    FCGX_FPrintF(message.out, "X-Sendfile: %s\r\n\r\n", filename.c_str());
}
