#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void config_defaults(Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->port        = DEFAULT_PORT;
    strncpy(cfg->root,        DEFAULT_ROOT,       MAX_PATH_LEN - 1);
    cfg->max_threads = DEFAULT_THREADS;
    strncpy(cfg->log_file,    DEFAULT_LOG,        MAX_PATH_LEN - 1);
    strncpy(cfg->users_file,  DEFAULT_USERS,      MAX_PATH_LEN - 1);
    strncpy(cfg->upload_dir,  DEFAULT_UPLOAD_DIR, MAX_PATH_LEN - 1);
    cfg->upload_max_mb = 16;
    cfg->rate_limit    = 120; /* 120 req/min per IP */
    cfg->timeout_sec   = 30;
}

static void trim(char *s) {
    size_t len = strlen(s), i = 0;
    while (i < len && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, len - i + 1);
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) s[--len] = '\0';
}

int config_load(Config *cfg, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line, *val = eq + 1;
        trim(key); trim(val);

        /* Core */
        if      (strcmp(key, "port")          == 0) cfg->port        = atoi(val);
        else if (strcmp(key, "root")          == 0) strncpy(cfg->root,       val, MAX_PATH_LEN-1);
        else if (strcmp(key, "max_threads")   == 0) cfg->max_threads  = atoi(val);
        else if (strcmp(key, "log_file")      == 0) strncpy(cfg->log_file,   val, MAX_PATH_LEN-1);
        else if (strcmp(key, "users_file")    == 0) strncpy(cfg->users_file, val, MAX_PATH_LEN-1);
        /* TLS */
        else if (strcmp(key, "tls_cert")      == 0) strncpy(cfg->tls_cert,   val, MAX_PATH_LEN-1);
        else if (strcmp(key, "tls_key")       == 0) strncpy(cfg->tls_key,    val, MAX_PATH_LEN-1);
        else if (strcmp(key, "tls_port")      == 0) cfg->tls_port      = atoi(val);
        /* Security */
        else if (strcmp(key, "rate_limit")    == 0) cfg->rate_limit    = atoi(val);
        else if (strcmp(key, "timeout_sec")   == 0) cfg->timeout_sec   = atoi(val);
        /* Uploads */
        else if (strcmp(key, "upload_dir")    == 0) strncpy(cfg->upload_dir, val, MAX_PATH_LEN-1);
        else if (strcmp(key, "upload_max_mb") == 0) cfg->upload_max_mb = atoi(val);
        /* Virtual hosts: key starts with "vhost." */
        else if (strncmp(key, "vhost.", 6) == 0) {
            const char *hostname = key + 6;
            VHost *v = vhost_create(hostname, val);
            if (v) { v->next = cfg->vhosts; cfg->vhosts = v; }
        }
        /* Proxy rules: key starts with "proxy." */
        else if (strncmp(key, "proxy.", 6) == 0) {
            const char *prefix = key + 6; /* e.g. "/api" */
            ProxyRule *r = proxy_rule_create(prefix, val);
            if (r) { r->next = cfg->proxy_rules; cfg->proxy_rules = r; }
        }
    }

    fclose(fp);
    return 1;
}

void config_print(const Config *cfg) {
    printf("  port          = %d\n", cfg->port);
    printf("  root          = %s\n", cfg->root);
    printf("  max_threads   = %d\n", cfg->max_threads);
    printf("  log_file      = %s\n", cfg->log_file);
    printf("  users_file    = %s\n", cfg->users_file);
    printf("  rate_limit    = %d req/min\n", cfg->rate_limit);
    printf("  timeout_sec   = %d\n", cfg->timeout_sec);
    printf("  upload_dir    = %s (%d MB max)\n", cfg->upload_dir, cfg->upload_max_mb);
    if (cfg->tls_port)
        printf("  tls_port      = %d (cert=%s)\n", cfg->tls_port, cfg->tls_cert);
    for (VHost *v = cfg->vhosts; v; v = v->next)
        printf("  vhost         = %s → %s\n", v->hostname, v->root);
    for (ProxyRule *r = cfg->proxy_rules; r; r = r->next)
        printf("  proxy         = %s → %s\n", r->prefix, r->backend);
}

void config_free(Config *cfg) {
    vhost_free_all(cfg->vhosts);     cfg->vhosts      = NULL;
    proxy_rule_free_all(cfg->proxy_rules); cfg->proxy_rules = NULL;
}
