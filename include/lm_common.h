#ifndef LIKEMAKE_COMMON_H
#define LIKEMAKE_COMMON_H
#include <stddef.h>
#include <stdint.h>

typedef struct {char **items; size_t len, cap;} LMStrings;
void lm_add(LMStrings *v, const char *s);
void lm_free(LMStrings *v);
int lm_contains(const LMStrings *v, const char *s);
char *lm_dup(const char *s);
char *lm_trim(char *s);
char *lm_joinpath(const char *a, const char *b);
char *lm_getcwd(void);
int lm_chdir(const char *path);
int lm_isfile(const char *path);
int lm_isdir(const char *path);
int lm_mkdirs(const char *path);
int lm_move(const char *src, const char *dst, int overwrite);
int lm_remove_tree(const char *path);
int lm_listfiles(LMStrings *list);
uint64_t lm_hash_bytes(uint64_t h, const void *p, size_t n);
uint64_t lm_hash_text(uint64_t h, const char *s);
uint64_t lm_hash_file(uint64_t h, const char *path, int *ok);
void lm_error(const char *fmt, ...);
#endif
