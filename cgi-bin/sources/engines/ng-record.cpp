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
#define RECORDS_DECRYPT_DIR     (const char*)"/tmp/.rec-plain"
#define RECORDS_ROTATE_SIZE     (3)

extern std::string stGetEnvirVariables(FCGX_Request &request, const char *name);

static struct {
    int counts = 0;
    struct {
        std::string alias;
        std::string named;
    } records[RECORDS_ROTATE_SIZE];
} sCacheFiles;

static int randomId = 0;

static inline void addAlias(const std::string& alias, const std::string& named) {
    if (!sCacheFiles.records[sCacheFiles.counts].alias.empty()) {
        unlink(sCacheFiles.records[sCacheFiles.counts].alias.c_str());
    }
    sCacheFiles.records[sCacheFiles.counts].alias.assign(alias);
    sCacheFiles.records[sCacheFiles.counts].named.assign(named);
    int n = (++sCacheFiles.counts) % RECORDS_ROTATE_SIZE;
    sCacheFiles.counts = n;
}

static inline bool getAlias(const std::string& name, std::string& alias) {
    for (uint8_t i = 0; i < RECORDS_ROTATE_SIZE; ++i) {
        if (sCacheFiles.records[i].named == name) {
            alias.assign(sCacheFiles.records[i].alias);
            return true;
        }
    }
    return false;
}
static inline void putHeader(FCGX_Request &m, const char *fmt, long long v) {
    char b[96];
    snprintf(b, sizeof(b), fmt, v);
    FCGX_PutStr(b, (int)strlen(b), m.out); /* FCGX_FPrintF can't do %lld */
}

/* Decrypt `src` -> temp file -> open + unlink (anonymous) -> stream range -> close. */
static bool decryptor(FCGX_Request &message, const std::string &filename) {
    char tmp[64] = {0};
    
    std::string alias;
    bool exist = getAlias(filename, alias);
    if (!exist) {
        /**
         * This file is not exist in memory, so we need to decrypt it and store it in RAM
         * Then we can use `tmp` as filename
         * Random number between 50 and 200 for making random filename
         */
        int recNum = (rand() % (4321 - 1234 + 1) + 1234);
        snprintf(tmp, sizeof(tmp), RECORDS_DECRYPT_DIR "/.%d.mp4", (++randomId) + recNum);

        mkdir(RECORDS_DECRYPT_DIR, 0755);
        int rc = runCommands("%s \"%s\" \"%s\" \"%s\"", RECORDS_DECRYPT_TOOL, APP_SECRET_UNIQUE_FILE, filename.c_str(), tmp);
        if (rc != 0) {
            /**
             * Return 0 mean SUCCESS decryptiton, so we can use `tmp` as filename. 
             * Otherwise, return false to let lighttpd serve directly records in '/mnt/sdcard'
             */
            return false;
        }
        addAlias(tmp, filename);
    } else {
        /* If exist, we can use `alias` as filename and reuse this */
        snprintf(tmp, sizeof(tmp), "%s", alias.c_str());
    }

    struct stat st = {0};
    int fd = open(tmp, O_RDONLY);
    if (fd < 0) {
        HTTP_ResponseDataAsHTML(message, 500, "<h1>500</h1>");
        return true;
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
            return true;
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

    return true;
}

/*
    The path MUST-BE as format "/records/<yy-mm-dd>/[yy-mm-ddThh.mm.ss].mp4"
    So we need to resolve it into a canonical path under "/mnt/sdcard"
*/
void ngrRedirectPlace(FCGX_Request &message, const std::string &path) {
    std::string filename = path;
    filename.replace(0, std::string("/records").length(), RECORDS_ROOT_DIR);

    if (access(filename.c_str(), F_OK) != 0) {
        HTTP_ResponseDataAsHTML(message, 404, "<h1>404</h1>");
        return;
    }

    if (decryptor(message, filename)) {
        return;
    }

    /* Plain clip: let lighttpd serve it (Content-Type/Length/Range all handled) */
    FCGX_FPrintF(message.out, "Status: 200 OK\r\n");
    FCGX_FPrintF(message.out, "Cache-Control: no-store\r\n");
    FCGX_FPrintF(message.out, "X-Sendfile: %s\r\n\r\n", filename.c_str());
}

void ngrCleanup(void) {
    for (uint8_t i = 0; i < RECORDS_ROTATE_SIZE; ++i) {
        const char *alias = sCacheFiles.records[i].alias.c_str();
        if (access(alias, F_OK) == 0) {
            unlink(alias);
        }
        sCacheFiles.counts = 0;
        sCacheFiles.records[i].alias.clear();
        sCacheFiles.records[i].named.clear();
    }
}