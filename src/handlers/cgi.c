#include "handlers/cgi.h"
#include "http/response.h"
#include "http/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cgi_is_script(const char *path) {
    const char *dot = strrchr(path, '.'); if (!dot) return 0; dot++;
    return strcasecmp(dot,"py")==0 || strcasecmp(dot,"sh")==0 ||
           strcasecmp(dot,"pl")==0 || strcasecmp(dot,"rb")==0 ||
           strcasecmp(dot,"php")==0;
}

static const char *pick_interp(const char *ext) {
    if (strcasecmp(ext,"py")==0)  return "python3";
    if (strcasecmp(ext,"rb")==0)  return "ruby";
    if (strcasecmp(ext,"pl")==0)  return "perl";
    if (strcasecmp(ext,"php")==0) return "php";
    if (strcasecmp(ext,"sh")==0)  return "sh";
    return NULL;
}

static void send_output(RequestCtx *ctx, const char *out, size_t len) {
    int has_hdrs = len > 0 &&
        (strncasecmp(out, "Content-Type:", 13) == 0 ||
         strncasecmp(out, "Status:",        7) == 0);
    if (has_hdrs) {
        io_write_all(&ctx->io, "HTTP/1.1 200 OK\r\n", 17);
        io_write_all(&ctx->io, out, len);
    } else {
        resp_string(ctx, 200, "text/html; charset=utf-8", out, (int)len);
        return;
    }
    ctx->response_sent = 1;
}

#ifdef _WIN32
void handler_cgi(RequestCtx *ctx, const char *script_path) {
    const char *dot = strrchr(script_path, '.'); if (!dot) { resp_error(ctx, 500); return; }
    const char *interp = pick_interp(dot+1); if (!interp) { resp_error(ctx, 500); return; }
    char cmd[MAX_PATH_LEN*2]; snprintf(cmd, sizeof(cmd), "%s \"%s\"", interp, script_path);
    SetEnvironmentVariableA("REQUEST_METHOD", ctx->req.method);
    SetEnvironmentVariableA("REQUEST_URI",    ctx->req.uri);
    SetEnvironmentVariableA("REMOTE_ADDR",    ctx->client_ip);
    SetEnvironmentVariableA("SERVER_SOFTWARE","chttp/1.0");
    const char *qs = strchr(ctx->req.uri, '?');
    SetEnvironmentVariableA("QUERY_STRING", qs ? qs+1 : "");
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hR, hW; if (!CreatePipe(&hR, &hW, &sa, 0)) { resp_error(ctx, 500); return; }
    SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si = { sizeof(si) }; si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hW; si.hStdError = hW; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        { CloseHandle(hR); CloseHandle(hW); resp_error(ctx, 500); return; }
    CloseHandle(hW);
    char tmp[8192]; char *output = NULL; size_t total = 0; DWORD got;
    while (ReadFile(hR, tmp, sizeof(tmp), &got, NULL) && got > 0) {
        output = (char *)realloc(output, total+got);
        memcpy(output+total, tmp, got); total += got;
    }
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hR);
    if (!output) { resp_error(ctx, 500); return; }
    send_output(ctx, output, total); free(output);
}
#else
#include <unistd.h>
#include <sys/wait.h>
void handler_cgi(RequestCtx *ctx, const char *script_path) {
    const char *dot = strrchr(script_path, '.'); if (!dot) { resp_error(ctx, 500); return; }
    const char *interp = pick_interp(dot+1); if (!interp) { resp_error(ctx, 500); return; }
    int pfd[2]; if (pipe(pfd) < 0) { resp_error(ctx, 500); return; }
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); resp_error(ctx, 500); return; }
    if (pid == 0) {
        close(pfd[0]); dup2(pfd[1], STDOUT_FILENO); dup2(pfd[1], STDERR_FILENO); close(pfd[1]);
        setenv("REQUEST_METHOD",  ctx->req.method, 1);
        setenv("REQUEST_URI",     ctx->req.uri,    1);
        setenv("REMOTE_ADDR",     ctx->client_ip,  1);
        setenv("SERVER_SOFTWARE", "chttp/1.0",     1);
        const char *qs = strchr(ctx->req.uri, '?');
        setenv("QUERY_STRING", qs ? qs+1 : "", 1);
        execl(interp, interp, script_path, (char *)NULL); _exit(1);
    }
    close(pfd[1]);
    char tmp[8192]; char *output = NULL; size_t total = 0; ssize_t n;
    while ((n = read(pfd[0], tmp, sizeof(tmp))) > 0) {
        output = (char *)realloc(output, total+(size_t)n);
        memcpy(output+total, tmp, (size_t)n); total += (size_t)n;
    }
    close(pfd[0]); int st; waitpid(pid, &st, 0);
    if (!output || !total) { free(output); resp_error(ctx, 500); return; }
    send_output(ctx, output, total); free(output);
}
#endif
