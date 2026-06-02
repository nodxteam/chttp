#ifdef HAVE_ZLIB
#include "http/gzip.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>

int gzip_should_compress(const char *mime) {
    static const char *pre[]={"text/","application/javascript","application/json","application/xml","image/svg",NULL};
    for(int i=0;pre[i];i++) if(strncmp(mime,pre[i],strlen(pre[i]))==0) return 1;
    return 0;
}

long gzip_compress(const char *src,size_t src_len,char **dst_out,size_t *dst_len_out) {
    uLongf bound=compressBound((uLong)src_len)+32;
    char *dst=(char *)malloc(bound); if(!dst) return -1;
    z_stream zs; memset(&zs,0,sizeof(zs));
    if(deflateInit2(&zs,Z_BEST_SPEED,Z_DEFLATED,15+16,8,Z_DEFAULT_STRATEGY)!=Z_OK){free(dst);return -1;}
    zs.next_in=(Bytef*)src; zs.avail_in=(uInt)src_len;
    zs.next_out=(Bytef*)dst; zs.avail_out=(uInt)bound;
    if(deflate(&zs,Z_FINISH)!=Z_STREAM_END){deflateEnd(&zs);free(dst);return -1;}
    long out_len=(long)zs.total_out; deflateEnd(&zs);
    *dst_out=dst; *dst_len_out=(size_t)out_len; return out_len;
}
#endif
