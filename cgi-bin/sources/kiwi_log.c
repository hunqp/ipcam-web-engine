#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/stat.h>
#include "kiwi_log.h"

#define KNRM (const char*)"\x1B[0m"
#define KRED (const char*)"\x1B[31m"
#define KGRN (const char*)"\x1B[32m"
#define KYEL (const char*)"\x1B[33m"
#define KBLU (const char*)"\x1B[34m"
#define KCYN (const char*)"\x1B[36m"

struct t_Decoration {
    int level;
    const char *tag;
    const char *color;
};

static const struct t_Decoration decorations[] = {
    { SYS_LOG_INFO ,  (const char*)"INFO ",  KGRN },
    { SYS_LOG_WARN ,  (const char*)"WARN ",  KYEL },
    { SYS_LOG_DEBUG,  (const char*)"DEBUG",  KBLU },
    { SYS_LOG_ERROR,  (const char*)"ERROR",  KRED }
};

KIWI_DIARY_T Kiwi_Runtime = {
    .filename = NULL,
    .maxBytesCanBeHold = 50 * 1024,
    .mt = PTHREAD_MUTEX_INITIALIZER
};
KIWI_DIARY_T Kiwi_Journal = {
    .filename = NULL,
    .maxBytesCanBeHold = 10 * 1024,
    .mt = PTHREAD_MUTEX_INITIALIZER
};

static void rotateHalfContent(KIWI_DIARY_T *me) {
    FILE *fp = fopen(me->filename, "r");
    if (!fp) {
        return;
    }
    fseek(fp, 0, SEEK_END);
    size_t fullSize = ftell(fp);
    size_t haflSize = fullSize / 2;
    fseek(fp, haflSize, SEEK_SET);

    char *content = (char*)calloc(haflSize, sizeof(char));
    if (!content) {
        fclose(fp);
        return;
    }
    size_t readBytes = fread(content, sizeof(char), haflSize, fp);
    fclose(fp);

    char *ptr = NULL;
    for (size_t id = 0; id < readBytes; ++id) {
        if (content[id] == '\n') {
            ptr = &content[id + 1]; /* Point to after newline */
            break;
        }
    }

    fp = fopen(me->filename, "w");
    if (fp) {
        fwrite(ptr, sizeof(char), readBytes - (ptr - content), fp);
        fclose(fp);
    }

    free(content);
}

void Kiwi_DIARY_WriteLine(KIWI_DIARY_T *me, int level, const char *fmt, ...) {
    pthread_mutex_lock(&me->mt);

    const char *tag = decorations[level].tag;
    const char *color = decorations[level].color;

    #if 0
    printf("%s[%s]%s", color, tag, KNRM);
    #endif

    /* Print datetime, example is "2025-09-23 15:00:00" */
    char datetime[20];
    time_t ts = time(NULL);
    memset(datetime, 0, sizeof(datetime));
    strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", localtime(&ts));
    printf("[%s] ", datetime);

    /* Print content */
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    if (me->filename) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        stat(me->filename, &st);
        if (st.st_size > me->maxBytesCanBeHold) {
            rotateHalfContent(me);
        }

        FILE *fp = fopen(me->filename, "a");
        if (fp) {
            fprintf(fp, "[%s][%s] ", tag, datetime);
            va_start(args, fmt);
            vfprintf(fp, fmt, args);
            va_end(args);
            fclose(fp);
        }
    }
    pthread_mutex_unlock(&me->mt);
}