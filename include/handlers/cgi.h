#ifndef HANDLERS_CGI_H
#define HANDLERS_CGI_H

#include "http/context.h"

int  cgi_is_script(const char *path);
void handler_cgi(RequestCtx *ctx, const char *script_path);

#endif
