#ifndef LOGGER_H
#define LOGGER_H

#include "platform.h"

typedef struct {
    FILE    *fp;
    mutex_t  mutex;
} Logger;

Logger *logger_open(const char *path);
void    logger_log(Logger *log, const char *client_ip,
                   const char *method, const char *uri,
                   int status, long bytes);
void    logger_close(Logger *log);

#endif /* LOGGER_H */
