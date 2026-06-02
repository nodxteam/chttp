CC     = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude

# Optional features (uncomment to enable):
# CFLAGS  += -DHAVE_OPENSSL
# LDFLAGS += -lssl -lcrypto
# CFLAGS  += -DHAVE_ZLIB
# LDFLAGS += -lz

SRCS = \
  src/core/main.c          \
  src/core/server.c        \
  src/core/config.c        \
  src/core/thread_pool.c   \
  src/core/logger.c        \
  src/net/io.c             \
  src/net/tls.c            \
  src/http/parser.c        \
  src/http/response.c      \
  src/http/mime.c          \
  src/http/etag.c          \
  src/http/range.c         \
  src/http/gzip.c          \
  src/middleware/auth.c    \
  src/middleware/rate_limiter.c \
  src/middleware/vhost.c   \
  src/router/router.c      \
  src/handlers/login.c     \
  src/handlers/files.c     \
  src/handlers/upload.c    \
  src/handlers/cgi.c       \
  src/handlers/proxy.c

ifeq ($(OS),Windows_NT)
    TARGET   = chttp.exe
    LDFLAGS += -lws2_32
else
    TARGET   = chttp
    LDFLAGS += -lpthread
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f chttp chttp.exe
