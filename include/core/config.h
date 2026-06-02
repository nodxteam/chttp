#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

#include "platform.h"
#include "middleware/vhost.h"
#include "handlers/proxy.h"

#define DEFAULT_PORT       8080
#define DEFAULT_ROOT       "./www"
#define DEFAULT_THREADS    8
#define DEFAULT_LOG        "access.log"
#define DEFAULT_USERS      "users.conf"
#define DEFAULT_UPLOAD_DIR "./uploads"

typedef struct {
    int  port;
    char root[MAX_PATH_LEN];
    int  max_threads;
    char log_file[MAX_PATH_LEN];
    char users_file[MAX_PATH_LEN];

    char tls_cert[MAX_PATH_LEN];
    char tls_key[MAX_PATH_LEN];
    int  tls_port;

    int  rate_limit;
    int  timeout_sec;

    char upload_dir[MAX_PATH_LEN];
    int  upload_max_mb;

    VHost     *vhosts;
    ProxyRule *proxy_rules;
} Config;

void config_defaults(Config *cfg);
int  config_load(Config *cfg, const char *path);
void config_print(const Config *cfg);
void config_free(Config *cfg);

#endif /* CORE_CONFIG_H */
