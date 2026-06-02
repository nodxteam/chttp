#ifndef CGI_H
#define CGI_H

#include "io.h"
#include "http.h"

/* Returns 1 if path has a known script extension (.py .sh .pl .rb .php) */
int cgi_is_script(const char *path);

/* Execute script, stream output to client.
   Returns 0 on success, -1 on error (caller sends 500 on -1). */
int cgi_execute(IoCtx *io, const char *script_path,
                const HttpRequest *req,
                const char *client_ip,
                const char *conn);

#endif /* CGI_H */
