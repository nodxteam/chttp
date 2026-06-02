#ifndef AUTH_H
#define AUTH_H

#include "platform.h"

#define SESSION_TOKEN_LEN 64        /* 32 random bytes, hex-encoded */
#define SESSION_TTL_SEC   (30 * 60) /* 30-minute expiry */
#define MAX_USERS         64
#define MAX_USERNAME      64
#define MAX_PASSWORD      128

typedef struct Session {
    char           token[SESSION_TOKEN_LEN + 1];
    char           username[MAX_USERNAME];
    time_t         created_at;
    struct Session *next;
} Session;

typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} User;

typedef struct {
    Session *sessions;
    User     users[MAX_USERS];
    int      user_count;
    mutex_t  mutex;
} AuthManager;

AuthManager *auth_create(void);
int          auth_load_users(AuthManager *am, const char *path);
void         auth_add_user(AuthManager *am, const char *u, const char *p);
int          auth_validate(AuthManager *am, const char *u, const char *p);

/* Returns the token string (valid until the session is deleted). */
const char  *auth_create_session(AuthManager *am, const char *username);
Session     *auth_get_session(AuthManager *am, const char *token);
void         auth_delete_session(AuthManager *am, const char *token);

void         auth_destroy(AuthManager *am);

#endif /* AUTH_H */
