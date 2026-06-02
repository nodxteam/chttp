#include "auth.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

/* ── Cryptographic random bytes ──────────────────────────────────────────── */

#ifdef _WIN32
#include <wincrypt.h>
static void rand_bytes(unsigned char *buf, size_t n) {
    HCRYPTPROV hprov = 0;
    if (CryptAcquireContextA(&hprov, NULL, NULL,
                              PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(hprov, (DWORD)n, buf);
        CryptReleaseContext(hprov, 0);
    } else {
        /* Weak fallback — should not happen on modern Windows */
        srand((unsigned)time(NULL));
        for (size_t i = 0; i < n; i++) buf[i] = (unsigned char)rand();
    }
}
#else
#include <fcntl.h>
#include <unistd.h>
static void rand_bytes(unsigned char *buf, size_t n) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        size_t got = 0;
        while (got < n) {
            ssize_t r = read(fd, buf + got, n - got);
            if (r <= 0) break;
            got += (size_t)r;
        }
        close(fd);
    } else {
        srand((unsigned)time(NULL));
        for (size_t i = 0; i < n; i++) buf[i] = (unsigned char)rand();
    }
}
#endif

static void gen_token(char out[SESSION_TOKEN_LEN + 1]) {
    unsigned char bytes[32];
    rand_bytes(bytes, sizeof(bytes));
    for (int i = 0; i < 32; i++)
        sprintf(out + i * 2, "%02x", bytes[i]);
    out[SESSION_TOKEN_LEN] = '\0';
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

AuthManager *auth_create(void) {
    AuthManager *am = (AuthManager *)calloc(1, sizeof(AuthManager));
    if (!am) return NULL;
    mutex_init(&am->mutex);
    return am;
}

void auth_destroy(AuthManager *am) {
    if (!am) return;
    Session *s = am->sessions;
    while (s) { Session *nx = s->next; free(s); s = nx; }
    mutex_destroy(&am->mutex);
    free(am);
}

/* ── Users ───────────────────────────────────────────────────────────────── */

static void trim_inplace(char *s) {
    size_t i = 0, len = strlen(s);
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i) memmove(s, s + i, len - i + 1);
    len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) s[--len] = '\0';
}

int auth_load_users(AuthManager *am, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    char line[300];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *u = line, *p = eq + 1;
        trim_inplace(u);
        trim_inplace(p);
        auth_add_user(am, u, p);
    }
    fclose(fp);
    return 1;
}

void auth_add_user(AuthManager *am, const char *u, const char *p) {
    if (am->user_count >= MAX_USERS) return;
    strncpy(am->users[am->user_count].username, u, MAX_USERNAME - 1);
    strncpy(am->users[am->user_count].password, p, MAX_PASSWORD - 1);
    am->user_count++;
}

int auth_validate(AuthManager *am, const char *u, const char *p) {
    for (int i = 0; i < am->user_count; i++) {
        if (strcmp(am->users[i].username, u) == 0 &&
            strcmp(am->users[i].password, p) == 0)
            return 1;
    }
    return 0;
}

/* ── Sessions ────────────────────────────────────────────────────────────── */

const char *auth_create_session(AuthManager *am, const char *username) {
    Session *s = (Session *)calloc(1, sizeof(Session));
    if (!s) return NULL;
    gen_token(s->token);
    strncpy(s->username, username, MAX_USERNAME - 1);
    s->created_at = time(NULL);

    mutex_lock(&am->mutex);
    s->next      = am->sessions;
    am->sessions = s;
    mutex_unlock(&am->mutex);

    return s->token;
}

Session *auth_get_session(AuthManager *am, const char *token) {
    if (!token || !*token) return NULL;
    time_t now = time(NULL);

    mutex_lock(&am->mutex);
    /* Also prune expired sessions while we're here */
    Session **pp = &am->sessions;
    Session *found = NULL;
    while (*pp) {
        Session *s = *pp;
        if (now - s->created_at > SESSION_TTL_SEC) {
            *pp = s->next;
            free(s);
            continue;
        }
        if (!found && strcmp(s->token, token) == 0)
            found = s;
        pp = &(*pp)->next;
    }
    mutex_unlock(&am->mutex);
    return found;
}

void auth_delete_session(AuthManager *am, const char *token) {
    mutex_lock(&am->mutex);
    Session **pp = &am->sessions;
    while (*pp) {
        if (strcmp((*pp)->token, token) == 0) {
            Session *del = *pp;
            *pp = del->next;
            free(del);
            break;
        }
        pp = &(*pp)->next;
    }
    mutex_unlock(&am->mutex);
}
