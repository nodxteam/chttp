#include "logger.h"
#include <stdlib.h>
#include <time.h>

Logger *logger_open(const char *path) {
    Logger *log = (Logger *)malloc(sizeof(Logger));
    if (!log) return NULL;

    log->fp = fopen(path, "a");
    if (!log->fp) {
        free(log);
        return NULL;
    }
    mutex_init(&log->mutex);
    return log;
}

void logger_log(Logger *log, const char *client_ip,
                const char *method, const char *uri,
                int status, long bytes) {
    if (!log) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%d/%b/%Y:%H:%M:%S %z", t);

    mutex_lock(&log->mutex);
    fprintf(log->fp, "%s - - [%s] \"%s %s\" %d %ld\n",
            client_ip, ts, method, uri, status, bytes);
    fflush(log->fp);
    mutex_unlock(&log->mutex);
}

void logger_close(Logger *log) {
    if (!log) return;
    fclose(log->fp);
    mutex_destroy(&log->mutex);
    free(log);
}
