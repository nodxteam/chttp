#ifndef CONFIG_H
#define CONFIG_H

#include "platform.h"
#include "vhost.h"
#include "proxy.h"

#define DEFAULT_PORT       8080
#define DEFAULT_ROOT       "./www"
#define DEFAULT_THREADS    8
#define DEFAULT_LOG        "access.log"
#define DEFAULT_USERS      "users.conf"
#define DEFAULT_UPLOAD_DIR "./uploads"

typedef struct {
    /* Core */
    int  port;
    char root[MAX_PATH_LEN];
    int  max_threads;
    char log_file[MAX_PATH_LEN];
    char users_file[MAX_PATH_LEN];

    /* TLS (optional, requires HAVE_OPENSSL) */
    char tls_cert[MAX_PATH_LEN];
    char tls_key[MAX_PATH_LEN];
    int  tls_port;   /* 0 = disabled */

    /* Security */
    int  rate_limit;   /* max requests per minute per IP; 0 = disabled */
    int  timeout_sec;  /* SO_RCVTIMEO; 0 = no timeout */

    /* Uploads */
    char upload_dir[MAX_PATH_LEN];
    int  upload_max_mb;

    /* Virtual hosts (parsed from "vhost.hostname = root" lines) */
    VHost *vhosts;

    /* Proxy rules (parsed from "proxy./prefix = http://host:port" lines) */
    ProxyRule *proxy_rules;
} Config;

void config_defaults(Config *cfg);
int  config_load(Config *cfg, const char *path);
void config_print(const Config *cfg);
void config_free(Config *cfg);

#endif /* CONFIG_H */
