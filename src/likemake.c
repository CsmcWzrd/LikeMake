/* LikeMake: a readable, deliberately small build-file interpreter.
 * C11, no third-party runtime libraries. */
#include "lm_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <inttypes.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#endif

#define HASH_INITIAL UINT64_C(14695981039346656037)

typedef struct {
    char *name;
    LMStrings deps, config, commands;
} LmSection;
typedef struct {
    LmSection *sections; size_t len,cap;
    LMStrings main_config;
    char *compiler, *linker, *output_dir;
} LmFile;
typedef enum {
    BLOCK_NONE,BLOCK_DEFAULTS,BLOCK_MAIN,BLOCK_OUTPUT,
    BLOCK_DEPS,BLOCK_CONFIG,BLOCK_COMMANDS
} LmBlock;

typedef struct {char *data;size_t len,cap;} Text;
static void append(Text *t, const char *s) {
    size_t n=strlen(s);
    if (t->len+n+1>t->cap) {
        size_t cap=t->cap?t->cap:128;
        while (cap<t->len+n+1) cap*=2;
        t->data=(char *)realloc(t->data,cap);
        if (!t->data) exit(2);
        t->cap=cap;
    }
    memcpy(t->data+t->len,s,n+1);t->len+=n;
}
static void append_char(Text *t,char c) {char b[2]={c,0};append(t,b);}
static char *read_line(FILE *f) {
    Text s={0};int c;
    while ((c=fgetc(f))!=EOF && c!='\n') append_char(&s,(char)c);
    if (c==EOF && s.len==0) {free(s.data);return NULL;}
    if (!s.data) return lm_dup("");
    return s.data;
}
static int is_header(const char *s) {
    size_t n=strlen(s);
    if (!strncmp(s,"Execution:",10)) return n>10;
    return n>1 && s[n-1]==':';
}
static int add_section(LmFile *m, const char *name, LMStrings *deps, LMStrings *config) {
    size_t i; LmSection *s;
    if (!*name) {lm_error("execution section needs a name");return 0;}
    for (i=0;i<m->len;i++) if (!strcmp(m->sections[i].name,name)) {
        lm_error("duplicate execution section '%s'",name);return 0;
    }
    if (m->len==m->cap) {
        size_t cap=m->cap?m->cap*2:8;
        LmSection *p=(LmSection *)realloc(m->sections,cap*sizeof(*p));
        if (!p) exit(2);
        m->sections=p;m->cap=cap;
    }
    s=&m->sections[m->len++];memset(s,0,sizeof(*s));
    s->name=lm_dup(name);s->deps=*deps;s->config=*config;
    memset(deps,0,sizeof(*deps));memset(config,0,sizeof(*config));
    return 1;
}
static int parse_file(LmFile *m, const char *filename) {
    FILE *f=fopen(filename,"rb");char *line;unsigned long line_no=0;
    LmBlock block=BLOCK_NONE;
    LMStrings deps={0},config={0};int ok=1;
    if (!f) {lm_error("cannot open '%s'",filename);return 0;}
    m->compiler=lm_dup("gcc");m->linker=lm_dup("gcc");
    while ((line=read_line(f))!=NULL) {
        char *s=lm_trim(line);++line_no;
        if (!*s) {block=BLOCK_NONE;free(line);continue;}
        if (is_header(s)) {
            size_t n=strlen(s);char *name;
            if (!strcmp(s,"Defaults:")) block=BLOCK_DEFAULTS;
            else if (!strcmp(s,"MainConfig:")) block=BLOCK_MAIN;
            else if (!strcmp(s,"OutputDir:")) block=BLOCK_OUTPUT;
            else if (!strcmp(s,"DependsOn:")) block=BLOCK_DEPS;
            else if (!strcmp(s,"Config:")) block=BLOCK_CONFIG;
            else {
                name=!strncmp(s,"Execution:",10)?s+10:s;
                if (name==s) s[n-1]=0;
                name=lm_trim(name);
                if (!add_section(m,name,&deps,&config)) {ok=0;free(line);break;}
                block=BLOCK_COMMANDS;
            }
            free(line);continue;
        }
        if (*s=='#') {free(line);continue;}
        switch(block) {
            case BLOCK_DEFAULTS: {
                char *p=s;
                while (*p && !isspace((unsigned char)*p)) p++;
                if (*p) *p++=0;
                p=lm_trim(p);
                if (!*p) {lm_error("line %lu: empty Defaults value",line_no);ok=0;}
                else if (!strcmp(s,"COMPILER")) {free(m->compiler);m->compiler=lm_dup(p);}
                else if (!strcmp(s,"LINKER")) {free(m->linker);m->linker=lm_dup(p);}
                else {lm_error("line %lu: unknown Defaults keyword '%s'",line_no,s);ok=0;}
            }break;
            case BLOCK_MAIN: lm_add(&m->main_config,s);break;
            case BLOCK_OUTPUT:
                if (m->output_dir) {lm_error("line %lu: OutputDir has multiple paths",line_no);ok=0;}
                else m->output_dir=lm_dup(s);
                break;
            case BLOCK_DEPS: lm_add(&deps,s);break;
            case BLOCK_CONFIG: lm_add(&config,s);break;
            case BLOCK_COMMANDS: lm_add(&m->sections[m->len-1].commands,s);break;
            default:lm_error("line %lu: content outside a section",line_no);ok=0;
        }
        free(line);if (!ok) break;
    }
    if (ferror(f)) {lm_error("read failure on '%s'",filename);ok=0;}
    fclose(f);lm_free(&deps);lm_free(&config);
    if (ok && !m->len) {lm_error("'%s' contains no execution sections",filename);ok=0;}
    return ok;
}
static void free_file(LmFile *m) {
    size_t i;
    for (i=0;i<m->len;i++) {
        free(m->sections[i].name);lm_free(&m->sections[i].deps);
        lm_free(&m->sections[i].config);lm_free(&m->sections[i].commands);
    }
    free(m->sections);lm_free(&m->main_config);
    free(m->compiler);free(m->linker);free(m->output_dir);
}
static int is_word_char(int c) {return isalnum((unsigned char)c)||c=='_';}
/* Expands whole-word COMPILER and LINKER, plus $(NAME) and ${NAME}. */
static char *expand_macros(const LmFile *m,const char *s) {
    Text t={0};size_t i=0;
    while (s[i]) {
        const char *replacement=NULL;size_t n=0;
        if ((i==0 || !is_word_char(s[i-1])) && !strncmp(s+i,"COMPILER",8)
            && !is_word_char(s[i+8])) {replacement=m->compiler;n=8;}
        else if ((i==0 || !is_word_char(s[i-1])) && !strncmp(s+i,"LINKER",6)
            && !is_word_char(s[i+6])) {replacement=m->linker;n=6;}
        else if (!strncmp(s+i,"$(COMPILER)",11)||!strncmp(s+i,"${COMPILER}",11))
            {replacement=m->compiler;n=11;}
        else if (!strncmp(s+i,"$(LINKER)",9)||!strncmp(s+i,"${LINKER}",9))
            {replacement=m->linker;n=9;}
        if (replacement) {append(&t,replacement);i+=n;}
        else append_char(&t,s[i++]);
    }
    if (!t.data) return lm_dup("");
    return t.data;
}
static char *config_flags(const LmFile *m,const LmSection *s) {
    Text t={0};size_t i,j;
    for (i=0;i<s->config.len;i++) {
        const char *item=s->config.items[i];
        if (!strcmp(item,"MainConfig")) {
            for (j=0;j<m->main_config.len;j++) {
                char *expanded=expand_macros(m,m->main_config.items[j]);
                if (t.len) append(&t," ");
                append(&t,expanded);free(expanded);
            }
        } else {
            char *expanded=expand_macros(m,item);
            if (t.len) append(&t," ");
                append(&t,expanded);free(expanded);
        }
    }
    return t.data?t.data:lm_dup("");
}
static char *command_body(const char *line,int *print,int *silent) {
    const char *p=line;
    *print=1;*silent=0;
    if (!strncmp(p,"!#",2)) {*print=0;*silent=1;p+=2;}
    else if (!strncmp(p,"~#",2)) {*print=1;p+=2;}
    while (*p && isspace((unsigned char)*p)) p++;
    return lm_dup(p);
}
static char *make_command(const LmFile *m,const char *raw,const char *flags) {
    char *expanded=expand_macros(m,raw);
    Text t={0};append(&t,expanded);
    if (*flags && strcmp(raw,"@clean")) {append(&t," ");append(&t,flags);}
    free(expanded);return t.data;
}
static int read_state(const char *name, uint64_t expected) {
    FILE *f;unsigned long long value=0;int match;
    char path[128];uint64_t h=lm_hash_text(HASH_INITIAL,name);
    snprintf(path,sizeof(path),".likemake/stamp-%016" PRIx64 ".txt",h);
    f=fopen(path,"r");if (!f) return 0;
    match=fscanf(f,"%llx",&value)==1 && (uint64_t)value==expected;
    fclose(f);return match;
}
static int write_state(const char *name,uint64_t hash) {
    FILE *f;char path[128];uint64_t h=lm_hash_text(HASH_INITIAL,name);
    if (!lm_mkdirs(".likemake")) {lm_error("cannot create .likemake");return 0;}
    snprintf(path,sizeof(path),".likemake/stamp-%016" PRIx64 ".txt",h);
    f=fopen(path,"w");if (!f) return 0;
    if (fprintf(f,"%016" PRIx64 "\n",hash)<0) {fclose(f);return 0;}
    return fclose(f)==0;
}
static int section_signature(const LmFile *m,const LmSection *s,uint64_t *out) {
    uint64_t h=lm_hash_text(HASH_INITIAL,s->name);size_t i;
    char *flags=config_flags(m,s);
    h=lm_hash_text(h,flags);h=lm_hash_text(h,m->compiler);h=lm_hash_text(h,m->linker);
    h=lm_hash_text(h,m->output_dir?m->output_dir:"");
    for (i=0;i<s->commands.len;i++) {
        char *p=make_command(m,s->commands.items[i],flags);
        h=lm_hash_text(h,p);free(p);
    }
    for (i=0;i<s->deps.len;i++) {
        const char *dep=s->deps.items[i];char *fallback=NULL;int ok=1;
        h=lm_hash_text(h,dep);
        if (!lm_isfile(dep) && m->output_dir) {
            fallback=lm_joinpath(m->output_dir,dep);
            if (lm_isfile(fallback)) dep=fallback;
        }
        if (!lm_isfile(dep)) {
            lm_error("section '%s': missing dependency '%s'",s->name,s->deps.items[i]);
            free(flags);free(fallback);return 0;
        }
        {
            struct stat st;
            if (stat(dep,&st)!=0) ok=0;
            else {
                h=lm_hash_bytes(h,&st.st_mtime,sizeof(st.st_mtime));
                h=lm_hash_bytes(h,&st.st_size,sizeof(st.st_size));
                h=lm_hash_file(h,dep,&ok);
            }
        }
        free(fallback);
        if (!ok) {lm_error("cannot read dependency '%s'",s->deps.items[i]);free(flags);return 0;}
    }
    free(flags);*out=h;return 1;
}
static int shell_run(const char *cmd, int silent) {
    int rc;char *command=NULL;
    if (silent) {
        Text t={0};append(&t,"(");append(&t,cmd);append(&t,") > ");
#ifdef _WIN32
        append(&t,"NUL 2>&1");
#else
        append(&t,"/dev/null 2>&1");
#endif
        command=t.data;
    }
    fflush(stdout);fflush(stderr);
    rc=system(command?command:cmd);free(command);
    if (rc==-1) return 127;
#ifdef _WIN32
    return rc;
#else
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    if (WIFSIGNALED(rc)) return 128+WTERMSIG(rc);
    return 127;
#endif
}
static int load_manifest(LMStrings *tracked) {
    FILE *f=fopen(".likemake/artifacts.txt","rb");char *line;
    if (!f) return 1;
    while ((line=read_line(f))!=NULL) {
        char *name=lm_trim(line);
        /* Only top-level leaf filenames; never trust paths from a stale manifest. */
        if (*name && strcmp(name,".") && strcmp(name,"..") &&
            !strchr(name,'/') && !strchr(name,'\\') &&
            strcmp(name,".likemake") && !lm_contains(tracked,name)) lm_add(tracked,name);
        free(line);
    }
    fclose(f);return 1;
}
static int save_manifest(const LMStrings *tracked) {
    FILE *f;size_t i;
    if (!lm_mkdirs(".likemake")) return 0;
    f=fopen(".likemake/artifacts.txt","wb");if (!f) return 0;
    for (i=0;i<tracked->len;i++) if (fprintf(f,"%s\n",tracked->items[i])<0) {
        fclose(f);return 0;
    }
    return fclose(f)==0;
}
static int restore_artifacts(const LmFile *m,const LMStrings *tracked, int *missing) {
    size_t i;*missing=0;
    for (i=0;i<tracked->len;i++) {
        const char *name=tracked->items[i];char *src=lm_joinpath(m->output_dir,name);
        if (lm_isfile(src)) {
            if (lm_isfile(name)) {
                lm_error("'%s' exists both in working directory and OutputDir; refusing overwrite",name);
                free(src);return 0;
            }
            if (!lm_move(src,name,0)) {
                lm_error("cannot stage '%s' from OutputDir",name);free(src);return 0;
            }
        } else if (!lm_isfile(name)) ++*missing;
        free(src);
    }
    return 1;
}
static int relocate_artifacts(const LmFile *m,const LMStrings *original,
                              const LMStrings *tracked) {
    LMStrings current={0},new_tracked={0};size_t i;int ok=1;
    if (!lm_listfiles(&current)) return 0;
    if (!lm_mkdirs(m->output_dir)) {lm_error("cannot create OutputDir '%s'",m->output_dir);lm_free(&current);return 0;}
    for (i=0;i<current.len;i++) {
        const char *file=current.items[i];char *dst;
        if (lm_contains(original,file) && !lm_contains(tracked,file)) continue;
        if (!strcmp(file,".likemake")) continue;
        dst=lm_joinpath(m->output_dir,file);
        if (lm_isfile(dst)) {
            /* Destinations for *tracked* files are eligible for replacement. */
            if (!lm_contains(tracked,file)) {
                lm_error("OutputDir already contains unrelated file '%s'; leaving file in place",file);
                ok=0;free(dst);continue;
            }
        }
        if (!lm_move(file,dst,lm_contains(tracked,file))) {
            lm_error("could not move generated file '%s' to '%s'",file,dst);
            ok=0;
        } else lm_add(&new_tracked,file);
        free(dst);
    }
    if (!save_manifest(&new_tracked)) {lm_error("unable to save artifact manifest");ok=0;}
    lm_free(&current);lm_free(&new_tracked);return ok;
}
static int clean_generated(const LmFile *m,const LMStrings *tracked) {
    size_t i;int ok=1;
    for (i=0;i<tracked->len;i++) {
        char *dest=m->output_dir?lm_joinpath(m->output_dir,tracked->items[i]):NULL;
        if (lm_isfile(tracked->items[i]) && remove(tracked->items[i])!=0) ok=0;
        if (dest && lm_isfile(dest) && remove(dest)!=0) ok=0;
        free(dest);
    }
    if (lm_isdir(".likemake") && !lm_remove_tree(".likemake")) ok=0;
    return ok;
}
static int run_section(const LmFile *m,const LmSection *s,int dry,int force,
                       int invalidated,const LMStrings *tracked,int *did_clean) {
    size_t j;int have_deps=s->deps.len>0;uint64_t hash=0;
    char *flags;
    if (!section_signature(m,s,&hash)) return 0;
    if (have_deps && !force && !invalidated && read_state(s->name,hash)) {
        printf("[LikeMake] %s: up to date\n",s->name);return 1;
    }
    printf("[LikeMake] %s\n",s->name);fflush(stdout);
    flags=config_flags(m,s);
    for (j=0;j<s->commands.len;j++) {
        int print,silent,rc;char *raw=command_body(s->commands.items[j],&print,&silent);
        char *cmd=make_command(m,raw,flags);
        if (!*raw) {free(raw);free(cmd);continue;}
        if (print) {printf("%s\n",cmd);fflush(stdout);}
        if (!dry) {
            if (!strcmp(raw,"@clean")) {
                rc=clean_generated(m,tracked)?0:1;
                if (rc==0) *did_clean=1;
            } else rc=shell_run(cmd,silent);
            if (rc) {
                lm_error("section '%s': command failed (exit %d)",s->name,rc);
                free(raw);free(cmd);free(flags);return 0;
            }
        }
        free(raw);free(cmd);
    }
    free(flags);
    if (have_deps && !dry && !*did_clean && !write_state(s->name,hash)) {
        lm_error("could not store dependency state for '%s'",s->name);return 0;
    }
    return 1;
}
static void usage(void) {
    puts("LikeMake - readable cross-platform build runner\n"
         "Usage: LikeMake [-c SUBFOLDER] [--file LikeMakefile] [--list] [--force] [--dry-run] [SECTION ...]\n"
         "No SECTION: run the first execution section (like make).\n"
         "-c SUBFOLDER: explicitly change into that directory before reading its build file.\n"
         "--file NAME: choose a filename in current directory (no path separators).\n"
         "--list: list named sections without executing.\n"
         "--force: ignore saved dependency signatures.\n"
         "--dry-run: display commands without executing or moving artifacts.\n"
         "Built-in execution command: @clean (remove tracked generated files and state).\n"
         "Build file names searched by default: LikeMakefile, Likemakefile.");
}
int main(int argc,char **argv) {
    int i,dry=0,force=0,list=0,result=0,missing=0,did_clean=0,anything=0;
    const char *dir=NULL,*file=NULL;
    LMStrings names={0},original={0},tracked={0};
    LmFile m={0};
    for (i=1;i<argc;i++) {
        if (!strcmp(argv[i],"-h")||!strcmp(argv[i],"--help")) {usage();return 0;}
        else if (!strcmp(argv[i],"-c") && i+1<argc) dir=argv[++i];
        else if (!strcmp(argv[i],"--file") && i+1<argc) file=argv[++i];
        else if (!strcmp(argv[i],"--list")) list=1;
        else if (!strcmp(argv[i],"--force")) force=1;
        else if (!strcmp(argv[i],"--dry-run")||!strcmp(argv[i],"-n")) dry=1;
        else if (argv[i][0]=='-' && argv[i][1]) {lm_error("unknown option '%s'",argv[i]);return 2;}
        else lm_add(&names,argv[i]);
    }
    if (dir && lm_chdir(dir)!=0) {lm_error("cannot change directory to '%s'",dir);return 2;}
    if (!file) file=lm_isfile("LikeMakefile")?"LikeMakefile":"Likemakefile";
    if (strchr(file,'/')||strchr(file,'\\')||!strcmp(file,".")||!strcmp(file,"..")) {
        lm_error("--file must be a filename within the current directory");return 2;
    }
    if (!parse_file(&m,file)) {result=2;goto done;}
    if (list) {
        size_t j;for (j=0;j<m.len;j++) printf("%s\n",m.sections[j].name);
        goto done;
    }
    for (i=0;i<(int)names.len;i++) {
        size_t j;int found=0;
        for (j=0;j<m.len;j++) if (!strcmp(names.items[i],m.sections[j].name)) found=1;
        if (!found) {lm_error("unknown execution section '%s'",names.items[i]);result=2;goto done;}
    }
    if (m.output_dir &&
        (!strcmp(m.output_dir,".") || !strcmp(m.output_dir,"./") ||
         !strcmp(m.output_dir,".\\"))) {
        lm_error("OutputDir cannot be the current directory");result=2;goto done;
    }
    if (m.output_dir && !dry) {
        if (!lm_listfiles(&original)) {lm_error("could not inspect current directory");result=2;goto done;}
        load_manifest(&tracked);
        if (!restore_artifacts(&m,&tracked,&missing)) {result=2;goto done;}
    }
    for (i=0;i<(int)(names.len ? names.len : 1);i++) {
        LmSection *s=NULL;size_t j;
        if (!names.len) s=&m.sections[0];
        else for (j=0;j<m.len;j++)
            if (!strcmp(names.items[i],m.sections[j].name)) {s=&m.sections[j];break;}
        if (!s) {result=2;break;}
        anything=1;
        did_clean=0;
        if (!run_section(&m,s,dry,force,missing>0,&tracked,&did_clean)) {result=1;break;}
    }
    if (m.output_dir && !dry) {
        /* Move back outputs even on failure, so subsequent invocations can recover. */
        if (!relocate_artifacts(&m,&original,&tracked)) result=1;
    }
    if (!anything && !result) result=2;
 done:
    lm_free(&names);lm_free(&original);lm_free(&tracked);free_file(&m);
    return result;
}
