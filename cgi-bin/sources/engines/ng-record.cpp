/*
    @Records download   (/records/<date>/<clip>.mp4 - auth done in main())

    Non-encrypted clip : X-Sendfile straight from the SD card.
    Encrypted clip      : decrypt to a temp file in tmpfs, open it, unlink it
                          immediately (now anonymous), stream the requested byte
                          range, close. The plaintext is freed from RAM the
                          moment the response ends - or on crash. No cache, no
                          metadata, nothing to clean up.

    Runs in its own FastCGI pool (lighttpd.conf "/records", max-procs 1) so
    requests are serialised and a slow decrypt never blocks the API pool.
*/
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

extern std::string stGetEnvirVariables(FCGX_Request &request, const char *name);

#define RECORDS_ROOT     "/mnt/sdcard"
#define RECORDS_MARKER   "mp4v2-encryption-flags"           /* MP4 'moov' comment atom on encrypted clips */
#define RECORDS_TOOL     "/oem/usr/bin/mp4-decrypt"
#define RECORDS_RAM_DIR  RAM_ROOT "/.rec-plain"

/* "/records/a/b.mp4" -> canonical path under /mnt/sdcard, or false (=> 404). */
static bool recordsResolve(const std::string &url, std::string &out) {
    if (url.compare(0, 9, "/records/") != 0) {
        return false;
    }
    std::string p;
    for (size_t i = 9; i < url.size(); ++i) {
        if (url[i] == '%' && i + 2 < url.size() &&
            isxdigit((unsigned char)url[i + 1]) && isxdigit((unsigned char)url[i + 2])) {
            char h[3] = { url[i + 1], url[i + 2], 0 };
            p += (char)strtol(h, NULL, 16);
            i += 2;
        } else {
            p += url[i];
        }
    }
    if (p.empty() || p.size() > 255 || p.find("..") != std::string::npos) {
        return false;
    }
    for (unsigned char c : p) {
        if (c < 0x20 || strchr("\\'\"`$;|&<>", c)) {   /* keep it shell-safe for mp4-decrypt */
            return false;
        }
    }

    char root[PATH_MAX], real[PATH_MAX];
    if (!realpath(RECORDS_ROOT, root) ||
        !realpath((std::string(root) + "/" + p).c_str(), real)) {
        return false;
    }
    const std::string prefix = std::string(root) + "/";
    if (strncmp(real, prefix.c_str(), prefix.size()) != 0) {
        return false;
    }
    struct stat st;
    if (stat(real, &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }
    out.assign(real);
    return true;
}

/* The recorder writes RECORDS_MARKER into the MP4 'moov' comment atom, which
   sits near one end of the file - a window at each end is enough. */
static bool recordsEncrypted(const std::string &path) {
    static const size_t L = sizeof(RECORDS_MARKER) - 1;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    char buf[256 * 1024];
    bool hit = false;
    for (int tail = 0; !hit && tail < 2; ++tail) {
        if (tail && fseek(fp, -(long)sizeof(buf), SEEK_END) != 0) {
            break;
        }
        size_t n = fread(buf, 1, sizeof(buf), fp);
        for (size_t i = 0; !hit && i + L <= n; ++i) {
            hit = memcmp(buf + i, RECORDS_MARKER, L) == 0;
        }
        if (tail == 0 && n < sizeof(buf)) {   /* small file fully scanned */
            break;
        }
    }
    fclose(fp);
    return hit;
}

static void putHeader(FCGX_Request &m, const char *fmt, long long v) {
    char b[96];
    snprintf(b, sizeof(b), fmt, v);
    FCGX_PutStr(b, (int)strlen(b), m.out);     /* FCGX_FPrintF can't do %lld */
}

/* Decrypt `src` -> temp file -> open + unlink (anonymous) -> stream range -> close. */
static void recordsStreamDecrypted(FCGX_Request &message, const std::string &src) {
    mkdir(RECORDS_RAM_DIR, 0700);
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), RECORDS_RAM_DIR "/w%ld.mp4", (long)getpid());
    unlink(tmp);

    if (access(RECORDS_TOOL, X_OK) != 0 ||
        runCommands("%s '%s' '%s'", RECORDS_TOOL, src.c_str(), tmp) != 0) {
        CGI_SYSW("Record decrypt failed: '%s'\r\n", src.c_str());
        unlink(tmp);
        HTTP_ResponseDataAsHTML(message, 500, "<h1>500</h1>");
        return;
    }

    int fd = open(tmp, O_RDONLY);
    unlink(tmp);                                /* freed on close() or on crash */
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) {
        if (fd >= 0) close(fd);
        HTTP_ResponseDataAsHTML(message, 500, "<h1>500</h1>");
        return;
    }
    const long long total = (long long)st.st_size;

    long long a = 0, b = total > 0 ? total - 1 : 0;
    bool partial = false;
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
        char buf[64 * 1024];
        long long left = b - a + 1;
        while (left > 0) {
            size_t want = left < (long long)sizeof(buf) ? (size_t)left : sizeof(buf);
            ssize_t n = read(fd, buf, want);
            if (n <= 0) {
                break;
            }
            if (FCGX_PutStr(buf, (int)n, message.out) != n) {   /* client went away */
                break;
            }
            left -= n;
        }
    }
    close(fd);                                  /* RAM reclaimed */
}

void HTTP_ServeRecordFile(FCGX_Request &message, const std::string &urlPath) {
    std::string src;
    if (!recordsResolve(urlPath, src)) {
        HTTP_ResponseDataAsHTML(message, 404, "<h1>404</h1>");
        return;
    }

    if (recordsEncrypted(src)) {
        recordsStreamDecrypted(message, src);
        return;
    }

    /* plain clip: let lighttpd serve it (Content-Type/Length/Range all handled) */
    FCGX_FPrintF(message.out, "Status: 200 OK\r\n");
    FCGX_FPrintF(message.out, "Cache-Control: no-store\r\n");
    FCGX_FPrintF(message.out, "X-Sendfile: %s\r\n\r\n", src.c_str());
}
