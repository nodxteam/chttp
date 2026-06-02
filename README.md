# chttp

A from-scratch HTTP/1.1 server written in C. Built as a learning project covering
sockets, threading, HTTP parsing, authentication, and common web server features.
Cross-platform: compiles on Windows (Winsock2/MinGW) and Linux/macOS (POSIX).

## Features

- HTTP/1.1 with keep-alive connections
- Thread pool for concurrent request handling
- Static file serving with MIME type detection
- Directory listing
- Form-based login with session cookies (30-minute TTL)
- ETag / If-None-Match caching (304 Not Modified)
- Range requests — 206 Partial Content for audio/video
- Gzip compression (optional, requires zlib)
- File upload via multipart/form-data (POST /upload)
- CGI script execution (.py, .sh, .pl, .rb, .php)
- Custom error pages (place 404.html, 500.html etc. in the document root)
- Virtual hosts — serve different directories per Host header
- Reverse proxy — forward URI prefixes to backend servers
- Per-IP rate limiting (sliding window)
- Request receive timeout (SO_RCVTIMEO)
- HTTPS / TLS (optional, requires OpenSSL)
- Access log in Combined Log Format

## Project structure

```
chttp/
├── include/
│   ├── platform.h          cross-platform socket/thread abstractions
│   ├── net/                network layer (IO abstraction, TLS)
│   ├── http/               HTTP protocol (parser, response helpers, context)
│   ├── middleware/         request pipeline (auth, rate limiter, vhost)
│   ├── router/             route table
│   ├── handlers/           request handlers (login, files, upload, cgi, proxy)
│   └── core/               server engine (config, thread pool, logger)
├── src/
│   └── (mirrors include/)
├── www/                    default document root
├── server.conf             server configuration
├── users.conf              user credentials
└── Makefile
```

The central object passed through the entire pipeline is `RequestCtx` (defined in
`include/http/context.h`). It holds the IO context, parsed request, resolved
document root, connection state, and a flag the handler sets once a response is
sent. Routes are registered in `src/core/server.c` with `router_add()` and
dispatched from the accept loop without any manual if-else chain.

## Building

A C99 compiler and make are required. On Windows, use MinGW-w64.

```sh
# Linux / macOS
make

# Windows (MinGW)
make
# or directly:
gcc -Wall -O2 -Iinclude -o chttp.exe src/core/main.c src/core/server.c \
    src/core/config.c src/core/thread_pool.c src/core/logger.c \
    src/net/io.c src/net/tls.c src/http/parser.c src/http/response.c \
    src/http/mime.c src/http/etag.c src/http/range.c src/http/gzip.c \
    src/middleware/auth.c src/middleware/rate_limiter.c src/middleware/vhost.c \
    src/router/router.c src/handlers/login.c src/handlers/files.c \
    src/handlers/upload.c src/handlers/cgi.c src/handlers/proxy.c \
    -lws2_32
```

### Optional features

Enable by adding flags to CFLAGS / LDFLAGS in the Makefile or on the command line:

| Feature | Flag | Link flag |
|---------|------|-----------|
| HTTPS / TLS | `-DHAVE_OPENSSL` | `-lssl -lcrypto` |
| Gzip compression | `-DHAVE_ZLIB` | `-lz` |

For TLS, generate a self-signed certificate for development:
```sh
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes
```

## Configuration

All settings live in `server.conf`. Defaults are used for any omitted key.

```ini
# Core
port        = 8080
root        = ./www
max_threads = 8
log_file    = access.log
users_file  = users.conf

# TLS (requires HAVE_OPENSSL)
# tls_port = 8443
# tls_cert = cert.pem
# tls_key  = key.pem

# Security
rate_limit  = 120    # max requests per minute per IP  (0 = disabled)
timeout_sec = 30     # receive timeout in seconds      (0 = no timeout)

# File uploads
upload_dir    = ./uploads
upload_max_mb = 16

# Virtual hosts
# vhost.example.com = ./vhosts/example

# Reverse proxy rules
# proxy./api     = http://127.0.0.1:3000
# proxy./backend = http://127.0.0.1:4000
```

## Users

Credentials are stored in plain text in `users.conf`. One entry per line:

```
admin   = changeme
alice   = hunter2
```

Change passwords before exposing the server to a network. The session token is a
32-byte cryptographically random value (CryptGenRandom on Windows, /dev/urandom
on Linux/macOS) and expires after 30 minutes of inactivity.

## Request flow

```
accept()
  |
  +-- TLS handshake (if HTTPS listener)
  |
  +-- Rate limit check  ->  429 if exceeded
  |
  +-- Read request headers + body
  |
  +-- Parse HTTP request
  |
  +-- Vhost resolution  ->  sets doc_root
  |
  +-- Proxy match  ->  forward to backend if prefix matches
  |
  +-- Route lookup (router_match)
  |     public route  ->  skip auth
  |     private route ->  check session cookie
  |                         no valid session  ->  302 /login
  |
  +-- Dispatch to handler
        GET  /login          handler_login_get
        POST /login          handler_login_post
        *    /logout         handler_logout
        POST /upload         handler_upload
        *    *               handler_files
                               |
                               +-- directory  -> index.html or listing
                               +-- CGI script -> exec + stream output
                               +-- static file -> ETag / Range / Gzip / plain
```

## Adding a route

Register in `src/core/server.c` before the accept loop:

```c
// router_add(method, pattern, handler_fn, is_public)
router_add("GET",  "/status", handler_status, 1);  // public
router_add("POST", "/api*",   handler_api,    0);  // requires login
```

Implement the handler in `src/handlers/`:

```c
// src/handlers/status.c
#include "handlers/status.h"
#include "http/response.h"

void handler_status(RequestCtx *ctx) {
    resp_string(ctx, 200, "application/json", "{\"ok\":true}", 11);
}
```

Pattern rules:
- Exact match: `"/login"` matches only `/login`
- Prefix match: `"/api*"` matches `/api`, `/api/users`, `/api/v2/items`
- Wildcard: `"*"` matches any URI (used as the catch-all)
- Method `"*"` matches any HTTP method

## CGI

Place executable scripts in the document root. Any file with a recognized
extension is executed instead of served:

| Extension | Interpreter |
|-----------|-------------|
| `.py` | python3 |
| `.sh` | sh |
| `.pl` | perl |
| `.rb` | ruby |
| `.php` | php |

Standard CGI environment variables are set: `REQUEST_METHOD`, `REQUEST_URI`,
`QUERY_STRING`, `REMOTE_ADDR`, `CONTENT_TYPE`, `CONTENT_LENGTH`.

## Virtual hosts

```ini
vhost.blog.example.com  = /var/www/blog
vhost.shop.example.com  = /var/www/shop
```

The `Host` header (port stripped) is matched case-insensitively. Unmatched
requests fall back to the default `root`.

## Reverse proxy

```ini
proxy./api      = http://127.0.0.1:3000
proxy./ws       = http://127.0.0.1:4000
```

The matching URI prefix is stripped before forwarding. An `X-Forwarded-For`
header containing the client IP is injected. The backend receives HTTP/1.0
with `Connection: close`.

## Limitations

This is a learning project, not a production server. Known limitations:

- Passwords stored in plain text (no hashing)
- No HTTP/2 or WebSocket support
- Reverse proxy does not support TLS backends
- CGI output is buffered in memory before sending
- Session store is in-memory only (lost on restart)
- No graceful shutdown signal handling
