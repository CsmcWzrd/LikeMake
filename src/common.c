#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "lm_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#define LM_SEP '\\'
#else
#include <unistd.h>
#include <dirent.h>
#define LM_SEP '/'
#endif

void lm_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); fprintf(stderr, "LikeMake: ");
    vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap);
}
char *lm_dup(const char *s) {
    size_t n = strlen(s) + 1; char *r = (char *)malloc(n);
    if (!r) {fputs("LikeMake: out of memory\n", stderr); exit(2);}
    memcpy(r, s, n); return r;
}
void lm_add(LMStrings *v, const char *s) {
    if (v->len == v->cap) {
        size_t c = v->cap ? v->cap * 2 : 8;
        char **p = (char **)realloc(v->items, c * sizeof(*p));
        if (!p) {fputs("LikeMake: out of memory\n", stderr); exit(2);}
        v->items = p; v->cap = c;
    }
    v->items[v->len++] = lm_dup(s);
}
void lm_free(LMStrings *v) {
    size_t i; for (i=0; i<v->len; ++i) free(v->items[i]);
    free(v->items); memset(v, 0, sizeof(*v));
}
int lm_contains(const LMStrings *v, const char *s) {
    size_t i; for (i=0; i<v->len; ++i) if (!strcmp(v->items[i], s)) return 1;
    return 0;
}
char *lm_trim(char *s) {
    char *p = s + strlen(s);
    while (p > s && isspace((unsigned char)p[-1])) --p;
    *p = 0; while (*s && isspace((unsigned char)*s)) ++s; return s;
}
char *lm_joinpath(const char *a, const char *b) {
    size_t na=strlen(a), nb=strlen(b);
    char *r=(char *)malloc(na+nb+2);
    if (!r) exit(2);
    memcpy(r,a,na); if (na && a[na-1]!='/' && a[na-1]!='\\') r[na++]=LM_SEP;
    memcpy(r+na,b,nb+1); return r;
}
char *lm_getcwd(void) {
    size_t cap = 512; char *p;
    for (;;) {
        p=(char *)malloc(cap); if (!p) exit(2);
#ifdef _WIN32
        if (_getcwd(p,(int)cap)) return p;
#else
        if (getcwd(p,cap)) return p;
#endif
        free(p); if (errno != ERANGE || cap > 65536) return NULL;
        cap *= 2;
    }
}
int lm_chdir(const char *path) {
#ifdef _WIN32
    return _chdir(path);
#else
    return chdir(path);
#endif
}
int lm_isfile(const char *path) {
    struct stat st;
#ifdef _WIN32
    return stat(path,&st)==0 && (st.st_mode & _S_IFMT)==_S_IFREG;
#else
    return stat(path,&st)==0 && S_ISREG(st.st_mode);
#endif
}
int lm_isdir(const char *path) {
    struct stat st;
#ifdef _WIN32
    return stat(path,&st)==0 && (st.st_mode & _S_IFMT)==_S_IFDIR;
#else
    return stat(path,&st)==0 && S_ISDIR(st.st_mode);
#endif
}
static int lm_mkdir_one(const char *s) {
#ifdef _WIN32
    return _mkdir(s)==0 || errno==EEXIST;
#else
    return mkdir(s,0777)==0 || errno==EEXIST;
#endif
}
int lm_mkdirs(const char *path) {
    char *s=lm_dup(path), *p=s;
    if (!*s) {free(s);return 0;}
#ifdef _WIN32
    if (isalpha((unsigned char)s[0]) && s[1]==':') {
        p=s+2;
        if (*p=='/' || *p=='\\') ++p; /* do not mkdir the drive prefix */
    } else if ((s[0]=='/' || s[0]=='\\') &&
               (s[1]=='/' || s[1]=='\\')) {
        p=s+2; /* UNC: skip server and share roots */
        while (*p && *p!='/' && *p!='\\') ++p;
        if (*p) ++p;
        while (*p && *p!='/' && *p!='\\') ++p;
        if (*p) ++p;
    } else if (*p=='/' || *p=='\\') ++p;
#else
    if (*p=='/') ++p;
#endif
    for (; *p; ++p) {
        if ((*p=='/' || *p=='\\') && p>s && p[1]) {
            char old=*p; *p=0;
            if (!lm_mkdir_one(s)) {free(s);return 0;} *p=old;
        }
    }
    {int ok=lm_mkdir_one(s); free(s); return ok;}
}
static int lm_copy_file(const char *src, const char *dst) {
    FILE *in=fopen(src,"rb"),*out; char buf[32768]; size_t n; int ok=1;
    if (!in) return 0;
    out=fopen(dst,"wb"); if (!out) {fclose(in);return 0;}
    while ((n=fread(buf,1,sizeof(buf),in))!=0) if (fwrite(buf,1,n,out)!=n) {ok=0;break;}
    if (ferror(in)) ok=0;
    if (fclose(out)!=0) ok=0;
    fclose(in);
    if (!ok) remove(dst);
    return ok;
}
int lm_move(const char *src, const char *dst, int overwrite) {
    if (!overwrite && lm_isfile(dst)) return 0;
#ifdef _WIN32
    if (MoveFileExA(src,dst,(overwrite ? MOVEFILE_REPLACE_EXISTING : 0)|MOVEFILE_COPY_ALLOWED)) return 1;
#else
    if (!overwrite && lm_isfile(dst)) return 0;
    if (rename(src,dst)==0) return 1;
    if (errno!=EXDEV) return 0;
#endif
    if (!lm_copy_file(src,dst)) return 0;
    if (remove(src)!=0) return 0;
    return 1;
}
int lm_listfiles(LMStrings *list) {
#ifdef _WIN32
    WIN32_FIND_DATAA d; HANDLE h=FindFirstFileA("*",&d);
    if (h==INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) lm_add(list,d.cFileName);
    } while(FindNextFileA(h,&d));
    FindClose(h);
#else
    DIR *dir=opendir("."); struct dirent *d;
    if (!dir) return 0;
    while ((d=readdir(dir))!=NULL) if (lm_isfile(d->d_name)) lm_add(list,d->d_name);
    closedir(dir);
#endif
    return 1;
}
int lm_remove_tree(const char *path) {
#ifdef _WIN32
    WIN32_FIND_DATAA data; HANDLE h;
    DWORD attrs=GetFileAttributesA(path);
    if (attrs==INVALID_FILE_ATTRIBUTES) return GetLastError()==ERROR_FILE_NOT_FOUND;
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return 0; /* Do not follow junctions. */
    char *mask=lm_joinpath(path,"*");
    h=FindFirstFileA(mask,&data); free(mask);
    if (h!=INVALID_HANDLE_VALUE) {
        do {
            char *child;
            if (!strcmp(data.cFileName,".") || !strcmp(data.cFileName,"..")) continue;
            child=lm_joinpath(path,data.cFileName);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!lm_remove_tree(child)) {free(child);FindClose(h);return 0;}
            } else if (remove(child)!=0) {free(child);FindClose(h);return 0;}
            free(child);
        }while(FindNextFileA(h,&data));
        FindClose(h);
    }
    return _rmdir(path)==0 || errno==ENOENT;
#else
    DIR *dir;struct dirent *d;struct stat root;
    if (lstat(path,&root)!=0) return errno==ENOENT;
    if (!S_ISDIR(root.st_mode)) return 0; /* Never traverse symlinks. */
    dir=opendir(path);
    if (!dir) return 0;
    while ((d=readdir(dir))!=NULL) {
        struct stat st; char *child;
        if (!strcmp(d->d_name,".") || !strcmp(d->d_name,"..")) continue;
        child=lm_joinpath(path,d->d_name);
        if (lstat(child,&st)!=0) {free(child);closedir(dir);return 0;}
        if (S_ISDIR(st.st_mode)) {
            if (!lm_remove_tree(child)) {free(child);closedir(dir);return 0;}
        } else if (remove(child)!=0) {free(child);closedir(dir);return 0;}
        free(child);
    }
    closedir(dir); return rmdir(path)==0;
#endif
}
uint64_t lm_hash_bytes(uint64_t h, const void *p, size_t n) {
    const unsigned char *s=(const unsigned char *)p; size_t i;
    for (i=0;i<n;++i) {h^=s[i];h*=UINT64_C(1099511628211);}
    return h;
}
uint64_t lm_hash_text(uint64_t h,const char *s) {
    h=lm_hash_bytes(h,s,strlen(s)); return lm_hash_bytes(h,"\0",1);
}
uint64_t lm_hash_file(uint64_t h,const char *path,int *ok) {
    FILE *f=fopen(path,"rb"); unsigned char buf[32768];size_t n;
    if (!f) {*ok=0;return h;}
    while ((n=fread(buf,1,sizeof(buf),f))!=0) h=lm_hash_bytes(h,buf,n);
    if (ferror(f)) *ok=0;
    fclose(f);return h;
}
