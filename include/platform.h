#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <io.h>
    #include <direct.h>

    typedef SOCKET sock_t;
    #define INVALID_SOCK INVALID_SOCKET
    #define sock_close   closesocket
    #define sock_error() WSAGetLastError()

    typedef HANDLE             thread_t;
    typedef CRITICAL_SECTION   mutex_t;
    typedef CONDITION_VARIABLE cond_t;

    #define mutex_init(m)    InitializeCriticalSection(m)
    #define mutex_lock(m)    EnterCriticalSection(m)
    #define mutex_unlock(m)  LeaveCriticalSection(m)
    #define mutex_destroy(m) DeleteCriticalSection(m)

    #define cond_init(c)      InitializeConditionVariable(c)
    #define cond_wait(c,m)    SleepConditionVariableCS((c),(m),INFINITE)
    #define cond_signal(c)    WakeConditionVariable(c)
    #define cond_broadcast(c) WakeAllConditionVariable(c)
    #define cond_destroy(c)   ((void)0)

    #define stat       _stat
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
    #define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
    #define strcasecmp  _stricmp
    #define strncasecmp _strnicmp
    #define strdup      _strdup
    #define strcasestr  _strcasestr_win
    #define mkdir(p,m)  _mkdir(p)

    static __inline char *_strcasestr_win(const char *h, const char *n) {
        if (!*n) return (char *)h;
        for (; *h; h++)
            if (_strnicmp(h, n, strlen(n)) == 0) return (char *)h;
        return NULL;
    }

    static __inline void *memmem(const void *h, size_t hl,
                                  const void *n, size_t nl) {
        if (nl == 0) return (void *)h;
        if (nl > hl)  return NULL;
        const char *hp = (const char *)h;
        const char *np = (const char *)n;
        for (size_t i = 0; i <= hl - nl; i++)
            if (hp[i] == np[0] && memcmp(hp + i, np, nl) == 0)
                return (void *)(hp + i);
        return NULL;
    }
#else
    #define _GNU_SOURCE  /* memmem, strcasestr */
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <errno.h>

    typedef int             sock_t;
    #define INVALID_SOCK    (-1)
    #define sock_close      close
    #define sock_error()    errno

    typedef pthread_t       thread_t;
    typedef pthread_mutex_t mutex_t;
    typedef pthread_cond_t  cond_t;

    #define mutex_init(m)    pthread_mutex_init((m), NULL)
    #define mutex_lock(m)    pthread_mutex_lock(m)
    #define mutex_unlock(m)  pthread_mutex_unlock(m)
    #define mutex_destroy(m) pthread_mutex_destroy(m)

    #define cond_init(c)      pthread_cond_init((c), NULL)
    #define cond_wait(c,m)    pthread_cond_wait((c),(m))
    #define cond_signal(c)    pthread_cond_signal(c)
    #define cond_broadcast(c) pthread_cond_broadcast(c)
    #define cond_destroy(c)   pthread_cond_destroy(c)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#define MAX_PATH_LEN 1024

#endif /* PLATFORM_H */
