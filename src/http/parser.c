#include "http/parser.h"
#include <string.h>
#include <stdio.h>

const char *http_status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        default:  return "Unknown";
    }
}

int http_parse_request(const char *buf, size_t len, HttpRequest *req) {
    memset(req, 0, sizeof(*req));
    const char *p = buf, *end = buf + len;

    const char *line_end = strstr(p, "\r\n");
    if (!line_end || line_end >= end) return -1;
    if (sscanf(p, "%15s %2047s %15s", req->method, req->uri, req->version) != 3)
        return -1;
    p = line_end + 2;

    while (p < end) {
        line_end = strstr(p, "\r\n");
        if (!line_end || line_end >= end) break;
        if (line_end == p) { p += 2; break; }
        if (req->header_count < MAX_HEADERS) {
            const char *colon = (const char *)memchr(p, ':', (size_t)(line_end - p));
            if (colon) {
                size_t nl = (size_t)(colon - p);
                if (nl > 255) nl = 255;
                memcpy(req->header_names[req->header_count], p, nl);
                req->header_names[req->header_count][nl] = '\0';
                const char *val = colon + 1;
                while (val < line_end && *val == ' ') val++;
                size_t vl = (size_t)(line_end - val);
                if (vl > 1023) vl = 1023;
                memcpy(req->header_values[req->header_count], val, vl);
                req->header_values[req->header_count][vl] = '\0';
                req->header_count++;
            }
        }
        p = line_end + 2;
    }

    if (p < end) { req->body = (char *)p; req->body_len = (size_t)(end - p); }
    return 0;
}

const char *http_get_header(const HttpRequest *req, const char *name) {
    for (int i = 0; i < req->header_count; i++)
        if (strcasecmp(req->header_names[i], name) == 0)
            return req->header_values[i];
    return NULL;
}
