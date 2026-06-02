#include "server.h"
#include "config.h"

int main(int argc, char *argv[]) {
    Config cfg;
    config_defaults(&cfg);

    const char *conf_path = "server.conf";
    if (argc > 1) conf_path = argv[1];

    if (!config_load(&cfg, conf_path))
        fprintf(stderr, "Warning: '%s' not found, using defaults\n", conf_path);

    printf("=== chttp/1.0 ===\n");
    config_print(&cfg);
    putchar('\n');

    server_start(&cfg);
    return 0;
}
