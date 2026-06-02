#include "cgi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cgi_is_script(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    dot++;
    return strcasecmp(dot, "py")  == 0 ||
           strcasecmp(dot, "sh")  == 0 ||
           strcasecmp(dot, "pl")  == 0 ||
           strcasecmp(dot, "rb")  == 0 ||
           strcasecmp(dot, "php") == 0;
}

/* ── Interpreter selection ─────────────────────────────────────────────── */
static const char *pick_interp(const char *ext) {
    if (strcasecmp(ext, "py")  == 0) return "python3";
    if (strcasecmp(ext, "rb")  == 0) return "ruby";
    if (strcasecmp(ext, "pl")  == 0) return "perl";
    if (strcasecmp(ext, "php") == 0) return "php";
    if (strcasecmp(ext, "sh")  == 0) return "sh";
    return NULL;
}

/* ── Build and send response from script output ─────────────────────────── */
static void send_cgi_output(IoCtx *io, const char *out, size_t out_len,
                             const char *conn) {
    /* CGI output starts with headers if first line looks like a header */
    int is_cgi_headers = out_len > 0 &&
        (strncasecmp(out, "Content-Type:", 13) == 0 ||
         strncasecmp(out, "Status:",        7) == 0 ||
         strncasecmp(out, "Location:",      9) == 0);

    if (is_cgi_headers) {
        /* Script provided its own headers; add HTTP status line */
        const char *status_line = "HTTP/1.1 200 OK\r\n";

        /* Check for Status: header */
        const char *status_hdr = strcasestr(out, "Status:");
        char status_line_buf[64];
        if (status_hdr) {
            int code = atoi(status_hdr + 7);
            snprintf(status_line_buf, sizeof(status_line_buf),
                     "HTTP/1.1 %d %s\r\n", code, http_status_text(code));
            status_line = status_line_buf;
        }

        io_write_all(io, status_line, strlen(status_line));
        io_write_all(io, out, out_len);
    } else {
        /* Wrap in a full HTTP response */
        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Connection: %s\r\n"
            "Server: chttp/1.0\r\n\r\n",
            out_len, conn);
        io_write_all(io, header, (size_t)hlen);
        io_write_all(io, out, out_len);
    }
}

/* ── Platform-specific execution ─────────────────────────────────────────── */

#ifdef _WIN32

int cgi_execute(IoCtx *io, const char *script_path,
                const HttpRequest *req,
                const char *client_ip,
                const char *conn) {
    const char *dot = strrchr(script_path, '.');
    if (!dot) return -1;
    const char *interp = pick_interp(dot + 1);
    if (!interp) return -1;

    char cmd[MAX_PATH_LEN * 2];
    snprintf(cmd, sizeof(cmd), "%s \"%s\"", interp, script_path);

    /* CGI environment */
    SetEnvironmentVariableA("REQUEST_METHOD",  req->method);
    SetEnvironmentVariableA("REQUEST_URI",     req->uri);
    SetEnvironmentVariableA("REMOTE_ADDR",     client_ip);
    SetEnvironmentVariableA("SERVER_SOFTWARE", "chttp/1.0");
    const char *qs = strchr(req->uri, '?');
    SetEnvironmentVariableA("QUERY_STRING", qs ? qs + 1 : "");
    const char *ct = http_get_header(req, "Content-Type");
    if (ct) SetEnvironmentVariableA("CONTENT_TYPE", ct);

    /* Stdout pipe */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hR, hW;
    if (!CreatePipe(&hR, &hW, &sa, 0)) return -1;
    SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hW;
    si.hStdError  = hW;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hR); CloseHandle(hW);
        return -1;
    }
    CloseHandle(hW);

    /* Collect output */
    char tmp[8192];
    char *output = NULL;
    size_t total = 0;
    DWORD got;
    while (ReadFile(hR, tmp, sizeof(tmp), &got, NULL) && got > 0) {
        output = (char *)realloc(output, total + got);
        memcpy(output + total, tmp, got);
        total += got;
    }
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hR);

    if (!output) return -1;
    send_cgi_output(io, output, total, conn);
    free(output);
    return 0;
}

#else  /* POSIX */

#include <unistd.h>
#include <sys/wait.h>

int cgi_execute(IoCtx *io, const char *script_path,
                const HttpRequest *req,
                const char *client_ip,
                const char *conn) {
    const char *dot = strrchr(script_path, '.');
    if (!dot) return -1;
    const char *interp = pick_interp(dot + 1);
    if (!interp) return -1;

    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        setenv("REQUEST_METHOD",  req->method, 1);
        setenv("REQUEST_URI",     req->uri,    1);
        setenv("REMOTE_ADDR",     client_ip,   1);
        setenv("SERVER_SOFTWARE", "chttp/1.0", 1);
        const char *qs = strchr(req->uri, '?');
        setenv("QUERY_STRING", qs ? qs + 1 : "", 1);
        const char *ct = http_get_header(req, "Content-Type");
        if (ct) setenv("CONTENT_TYPE", ct, 1);
        if (req->body_len > 0) {
            char cl[32];
            snprintf(cl, sizeof(cl), "%zu", req->body_len);
            setenv("CONTENT_LENGTH", cl, 1);
        }

        execl(interp, interp, script_path, (char *)NULL);
        _exit(1);
    }

    close(pipefd[1]);

    char tmp[8192];
    char *output = NULL;
    size_t total = 0;
    ssize_t n;
    while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) {
        output = (char *)realloc(output, total + (size_t)n);
        memcpy(output + total, tmp, (size_t)n);
        total += (size_t)n;
    }
    close(pipefd[0]);
    int status;
    waitpid(pid, &status, 0);

    if (!output || total == 0) { free(output); return -1; }
    send_cgi_output(io, output, total, conn);
    free(output);
    return 0;
}

#endif /* _WIN32 */
