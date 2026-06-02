#ifndef UPLOAD_H
#define UPLOAD_H

#include "io.h"

/* Parse multipart/form-data body and save files to upload_dir.
   Returns number of files saved, or -1 on error (response already sent). */
int upload_handle(IoCtx *io,
                  const char *body, size_t body_len,
                  const char *content_type,
                  const char *upload_dir,
                  long        max_bytes,
                  const char *conn);

#endif /* UPLOAD_H */
