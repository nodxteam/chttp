#include "http/etag.h"
#include <stdio.h>
#include <string.h>

void etag_generate(const struct stat *st, char *out, size_t out_sz) {
    snprintf(out, out_sz, "\"%lx-%lx\"",
             (unsigned long)st->st_mtime, (unsigned long)st->st_size);
}

int etag_matches(const char *ce, const char *se) {
    if(!ce||!se) return 0;
    if(strncmp(ce,"W/",2)==0) ce+=2;
    if(strncmp(se,"W/",2)==0) se+=2;
    if(strcmp(ce,"*")==0) return 1;
    char a[128],b[128]; strncpy(a,ce,127); strncpy(b,se,127);
    char *p; if(*a=='"'){memmove(a,a+1,strlen(a));} if((p=strchr(a,'"')))*p='\0';
             if(*b=='"'){memmove(b,b+1,strlen(b));} if((p=strchr(b,'"')))*p='\0';
    return strcmp(a,b)==0;
}
