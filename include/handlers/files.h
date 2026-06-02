#ifndef HANDLERS_FILES_H
#define HANDLERS_FILES_H

#include "http/context.h"

/* Catch-all handler: static files, CGI, dir listing */
void handler_files(RequestCtx *ctx);

#endif
