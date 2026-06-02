#include "http/range.h"
#include "http/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RangeSpec range_parse(const char *hval, long file_size) {
    RangeSpec r={0,-1,0};
    if(!hval||strncmp(hval,"bytes=",6)!=0) return r;
    const char *p=hval+6, *dash=strchr(p,'-'); if(!dash) return r;
    if(p==dash) { long n=atol(dash+1); if(n<=0) return r; r.start=file_size-n; r.end=file_size-1; }
    else { r.start=atol(p); r.end=(dash[1]?atol(dash+1):file_size-1); }
    if(r.start<0) r.start=0;
    if(r.end>=file_size) r.end=file_size-1;
    if(r.start>r.end) return r;
    r.valid=1; return r;
}

void range_send(IoCtx *io, const char *path, const char *mime,
                const char *conn, const RangeSpec *r, long file_size) {
    FILE *fp=fopen(path,"rb"); if(!fp) return;
    long length=r->end-r->start+1;
    char header[512];
    int hlen=snprintf(header,sizeof(header),
        "HTTP/1.1 206 Partial Content\r\n"
        "Content-Type: %s\r\nContent-Length: %ld\r\n"
        "Content-Range: bytes %ld-%ld/%ld\r\n"
        "Accept-Ranges: bytes\r\nConnection: %s\r\nServer: chttp/1.0\r\n\r\n",
        mime,length,r->start,r->end,file_size,conn);
    io_write_all(io,header,(size_t)hlen);
    if(fseek(fp,r->start,SEEK_SET)!=0){fclose(fp);return;}
    char buf[65536]; long rem=length;
    while(rem>0){size_t want=(size_t)(rem<(long)sizeof(buf)?rem:(long)sizeof(buf));
        size_t got=fread(buf,1,want,fp); if(!got) break;
        io_write_all(io,buf,got); rem-=(long)got;}
    fclose(fp);
}
