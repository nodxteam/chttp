#ifndef HANDLERS_LOGIN_H
#define HANDLERS_LOGIN_H

#include "http/context.h"

void handler_login_get(RequestCtx *ctx);
void handler_login_post(RequestCtx *ctx);
void handler_logout(RequestCtx *ctx);

#endif
