#include "handlers/login.h"
#include "http/response.h"
#include "http/parser.h"
#include "core/server.h"
#include "middleware/auth.h"
#include <string.h>
#include <stdlib.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static void url_decode(char *dst, const char *src, size_t dst_sz) {
    size_t di = 0;
    for (size_t i = 0; src[i] && di + 1 < dst_sz; i++) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst[di++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[i];
        }
    }
    dst[di] = '\0';
}

static int parse_cookie(const char *hdr, const char *name,
                          char *out, size_t out_sz) {
    if (!hdr) return 0;
    size_t nlen = strlen(name);
    const char *p = hdr;
    while (*p) {
        while (*p == ' ') p++;
        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            p += nlen + 1;
            size_t i = 0;
            while (p[i] && p[i] != ';' && i + 1 < out_sz) out[i] = p[i++];
            out[i] = '\0';
            return 1;
        }
        while (*p && *p != ';') p++;
        if (*p == ';') p++;
    }
    return 0;
}

static void parse_form_body(const char *body, size_t body_len,
                              char *out_user, size_t user_sz,
                              char *out_pass, size_t pass_sz) {
    out_user[0] = out_pass[0] = '\0';
    if (!body || !body_len) return;
    char buf[2048];
    size_t n = body_len < sizeof(buf)-1 ? body_len : sizeof(buf)-1;
    memcpy(buf, body, n); buf[n] = '\0';

    const char *p = buf;
    while (*p) {
        const char *eq = strchr(p, '='); if (!eq) break;
        const char *amp = strchr(eq+1, '&');
        if (!amp) amp = eq + 1 + strlen(eq + 1);
        char key[128]={0}, val[512]={0};
        size_t kl=(size_t)(eq-p), vl=(size_t)(amp-eq-1);
        if (kl>127) kl=127; if (vl>511) vl=511;
        memcpy(key, p, kl); memcpy(val, eq+1, vl);
        if      (strcmp(key, "username") == 0) url_decode(out_user, val, user_sz);
        else if (strcmp(key, "password") == 0) url_decode(out_pass, val, pass_sz);
        p = *amp ? amp+1 : amp;
    }
}

static void send_login_page(RequestCtx *ctx, int error) {
    const char *err = error
        ? "<p class=\"err\">Kullan&#x131;c&#x131; ad&#x131; veya &#x15f;ifre yanl&#x131;&#x15f;.</p>"
        : "";
    char body[4096];
    int blen = snprintf(body, sizeof(body),
        "<!DOCTYPE html><html lang=\"tr\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Giri&#x15f;</title>"
        "<style>*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:'Segoe UI',system-ui,sans-serif;background:#0f0f0f;"
        "display:flex;align-items:center;justify-content:center;min-height:100vh}"
        ".card{background:#1a1a1a;border:1px solid #333;border-radius:12px;"
        "padding:40px 48px;width:100%%;max-width:380px}"
        "h2{color:#e0e0e0;margin-bottom:24px;font-size:1.4rem}"
        "label{display:block;color:#888;font-size:.82rem;margin-bottom:6px}"
        "input{width:100%%;background:#111;border:1px solid #444;border-radius:6px;"
        "color:#e0e0e0;padding:10px 12px;font-size:.95rem;outline:none;transition:border .15s}"
        "input:focus{border-color:#58a6ff}.field{margin-bottom:16px}"
        "button{width:100%%;background:#238636;border:none;border-radius:6px;"
        "color:#fff;padding:11px;font-size:.95rem;cursor:pointer;margin-top:8px}"
        "button:hover{background:#2ea043}"
        ".err{color:#f85149;font-size:.85rem;margin-bottom:16px}"
        "</style></head><body><div class=\"card\">"
        "<h2>chttp &mdash; Giri&#x15f;</h2>%s"
        "<form method=\"POST\" action=\"/login\">"
        "<div class=\"field\"><label>Kullan&#x131;c&#x131; Ad&#x131;</label>"
        "<input name=\"username\" type=\"text\" autofocus></div>"
        "<div class=\"field\"><label>&#x15e;ifre</label>"
        "<input name=\"password\" type=\"password\"></div>"
        "<button type=\"submit\">Giri&#x15f; Yap</button>"
        "</form></div></body></html>", err);
    resp_string(ctx, 200, "text/html; charset=utf-8", body, blen);
}

/* ── Handlers ─────────────────────────────────────────────────────────────── */

void handler_login_get(RequestCtx *ctx) {
    char *qs = strchr(ctx->req.uri, '?');
    int show_error = qs && strstr(qs, "error=1");
    send_login_page(ctx, show_error);
}

void handler_login_post(RequestCtx *ctx) {
    char username[MAX_USERNAME]={0}, password[MAX_PASSWORD]={0};
    parse_form_body(ctx->req.body, ctx->req.body_len,
                    username, sizeof(username),
                    password, sizeof(password));

    if (auth_validate(ctx->server->auth, username, password)) {
        const char *tok = auth_create_session(ctx->server->auth, username);
        char sc[256];
        snprintf(sc, sizeof(sc),
                 "Set-Cookie: session=%s; HttpOnly; Path=/\r\n", tok);
        resp_redirect(ctx, 302, "/", sc);
    } else {
        resp_redirect(ctx, 302, "/login?error=1", NULL);
    }
}

void handler_logout(RequestCtx *ctx) {
    char tok[SESSION_TOKEN_LEN+1]={0};
    const char *ck = http_get_header(&ctx->req, "Cookie");
    if (parse_cookie(ck, "session", tok, sizeof(tok)))
        auth_delete_session(ctx->server->auth, tok);
    resp_redirect(ctx, 302, "/login",
                  "Set-Cookie: session=; HttpOnly; Path=/; Max-Age=0\r\n");
}

/* Helper used by server.c middleware to check auth */
int auth_check_cookie(RequestCtx *ctx) {
    char tok[SESSION_TOKEN_LEN+1]={0};
    const char *ck = http_get_header(&ctx->req, "Cookie");
    return parse_cookie(ck, "session", tok, sizeof(tok)) &&
           (auth_get_session(ctx->server->auth, tok) != NULL);
}
