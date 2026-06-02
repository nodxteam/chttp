#include "mime.h"
#include <string.h>

static const struct { const char *ext; const char *type; } table[] = {
    { "html",  "text/html; charset=utf-8" },
    { "htm",   "text/html; charset=utf-8" },
    { "css",   "text/css" },
    { "js",    "application/javascript" },
    { "mjs",   "application/javascript" },
    { "json",  "application/json" },
    { "xml",   "application/xml" },
    { "txt",   "text/plain; charset=utf-8" },
    { "md",    "text/markdown" },
    { "csv",   "text/csv" },
    { "png",   "image/png" },
    { "jpg",   "image/jpeg" },
    { "jpeg",  "image/jpeg" },
    { "gif",   "image/gif" },
    { "ico",   "image/x-icon" },
    { "svg",   "image/svg+xml" },
    { "webp",  "image/webp" },
    { "bmp",   "image/bmp" },
    { "pdf",   "application/pdf" },
    { "zip",   "application/zip" },
    { "gz",    "application/gzip" },
    { "tar",   "application/x-tar" },
    { "mp3",   "audio/mpeg" },
    { "ogg",   "audio/ogg" },
    { "wav",   "audio/wav" },
    { "mp4",   "video/mp4" },
    { "webm",  "video/webm" },
    { "woff",  "font/woff" },
    { "woff2", "font/woff2" },
    { "ttf",   "font/ttf" },
    { "otf",   "font/otf" },
    { NULL, NULL }
};

const char *mime_get(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || dot[1] == '\0') return "application/octet-stream";
    dot++;

    for (int i = 0; table[i].ext; i++) {
        if (strcasecmp(dot, table[i].ext) == 0)
            return table[i].type;
    }
    return "application/octet-stream";
}
