#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include "platform.h"

#define MAX_HEADERS  64
#define MAX_METHOD   16
#define MAX_URI      2048
#define MAX_VERSION  16
#define READ_BUF     8192

typedef struct {
    char   method[MAX_METHOD];
    char   uri[MAX_URI];
    char   version[MAX_VERSION];
    char   header_names[MAX_HEADERS][256];
    char   header_values[MAX_HEADERS][1024];
    int    header_count;
    char  *body;
    size_t body_len;
} HttpRequest;

const char *http_status_text(int code);
int         http_parse_request(const char *buf, size_t len, HttpRequest *req);
const char *http_get_header(const HttpRequest *req, const char *name);

#endif /* HTTP_PARSER_H */
