/* LikeMakefileBuilder: generates LikeMakefiles from source-file input. */
#include "lm_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>
#ifdef _WIN32
#define LM_DEFAULT_EXE ".exe"
#else
#define LM_DEFAULT_EXE ""
#endif

typedef struct {char *name;LMStrings depends,commands,configs;} Extra;
typedef struct {
    char *project,*target,*file,*output_dir,*compiler,*linker;
    LMStrings sources,cflags,ldflags;
    Extra *extras;size_t len,cap;int overwrite,msvc,interactive,compiler_selected,linker_selected;
} Options;
static void help(void) {
    puts("LikeMakefileBuilder - generate readable build recipes\n"
         "Usage: LikeMakefileBuilder [options]\n"
         "  --project NAME       logical project name (default: Application)\n"
         "  --source FILE        add a C source file (repeatable)\n"
         "  --target NAME        output executable (default: project name)\n"
         "  --output FILE        LikeMakefile or Likemakefile (default: LikeMakefile)\n"
         "  --output-dir DIR     generated artifact directory (default: build-output)\n"
         "  --compiler CMD       compiler command (default: gcc)\n"
         "  --linker CMD         linker command (default: gcc)\n"
         "  --cflag FLAG         extra compiler flag (repeatable)\n"
         "  --ldflag FLAG        extra linker flag (repeatable)\n"
         "  --msvc               generate MSVC cl/link-style commands\n"
         "  --section NAME       append custom section (repeatable)\n"
         "  --command STRING     add shell command to preceding custom section\n"
         "  --depends FILE       add file dependency to preceding custom section\n"
         "  --config LINE        add config line to preceding custom section\n"
         "  --interactive        prompt for project, target, files and output folder\n"
         "  --force              overwrite existing generated file\n"
         "  --help               show help\n"
         "Generates 'build' and 'clean' sections (clean invokes built-in @clean).\n"
         "For subproject commands, use --section sub --command 'LikeMake -c sub build'.");
}
static void set_string(char **slot,const char *value) {free(*slot);*slot=lm_dup(value);}
static int safe_line(const char *value) {
    return *value && !strchr(value,'\n') && !strchr(value,'\r');
}
static int safe_path(const char *value) {
    return safe_line(value) && !strchr(value,'"');
}
static char *quoted(const char *s) {
    size_t n=strlen(s);char *p=(char *)malloc(n+3);
    if (!p) exit(2);
    p[0]='"';memcpy(p+1,s,n);p[n+1]='"';p[n+2]=0;return p;
}
static char *object_name(const char *src,int msvc) {
    const char *base=src,*p=src;char name[64];size_t len;uint64_t hash;
    while (*p) {if (*p=='/'||*p=='\\') base=p+1;p++;}
    len=strlen(base);if (len>2 && base[len-2]=='.' && (base[len-1]=='c'||base[len-1]=='C')) len-=2;
    if (len>40) len=40;
    hash=lm_hash_text(UINT64_C(14695981039346656037),src);
    snprintf(name,sizeof(name),"%08" PRIx32,(uint32_t)(hash & UINT32_MAX));
    {char *result=(char *)malloc(len+strlen(name)+7);size_t i;
        if (!result) exit(2);
        for (i=0;i<len;i++) result[i]=isalnum((unsigned char)base[i])||base[i]=='_' ? base[i]:'_';
        result[len]=0;strcat(result,"_");strcat(result,name);strcat(result,msvc?".obj":".o");return result;
    }
}
static char *prompt(const char *label,const char *fallback) {
    char buf[8192],*t;
    printf("%s [%s]: ",label,fallback);fflush(stdout);
    if (!fgets(buf,sizeof(buf),stdin)) return lm_dup(fallback);
    t=lm_trim(buf);return lm_dup(*t?t:fallback);
}
static int parse(Options *o,int argc,char **argv) {
    int i;Extra *current=NULL;
    o->project=lm_dup("Application");o->file=lm_dup("LikeMakefile");
    o->compiler=lm_dup("gcc");o->linker=lm_dup("gcc");o->output_dir=lm_dup("build-output");
    for (i=1;i<argc;i++) {
        const char *k=argv[i],*v=NULL;
        if (!strcmp(k,"--help")||!strcmp(k,"-h")) {help();return -1;}
        if (!strcmp(k,"--force")) {o->overwrite=1;continue;}
        if (!strcmp(k,"--msvc")) {o->msvc=1;continue;}
        if (!strcmp(k,"--interactive")) {o->interactive=1;continue;}
        if (i+1>=argc) {lm_error("missing value for '%s'",k);return 0;}
        v=argv[++i];
        if (!safe_line(v)) {lm_error("invalid value for '%s'",k);return 0;}
        if (!strcmp(k,"--project")) set_string(&o->project,v);
        else if (!strcmp(k,"--target")) set_string(&o->target,v);
        else if (!strcmp(k,"--output")) set_string(&o->file,v);
        else if (!strcmp(k,"--output-dir")) set_string(&o->output_dir,v);
        else if (!strcmp(k,"--compiler")) {set_string(&o->compiler,v);o->compiler_selected=1;}
        else if (!strcmp(k,"--linker")) {set_string(&o->linker,v);o->linker_selected=1;}
        else if (!strcmp(k,"--source")) lm_add(&o->sources,v);
        else if (!strcmp(k,"--cflag")) lm_add(&o->cflags,v);
        else if (!strcmp(k,"--ldflag")) lm_add(&o->ldflags,v);
        else if (!strcmp(k,"--section")) {
            if (o->len==o->cap) {
                size_t cap=o->cap?o->cap*2:4;
                Extra *p=(Extra *)realloc(o->extras,cap*sizeof(*p));
                if (!p) exit(2);
                o->extras=p;o->cap=cap;
            }
            current=&o->extras[o->len++];memset(current,0,sizeof(*current));
            current->name=lm_dup(v);
        } else if (!strcmp(k,"--command") && current) lm_add(&current->commands,v);
        else if (!strcmp(k,"--depends") && current) lm_add(&current->depends,v);
        else if (!strcmp(k,"--config") && current) lm_add(&current->configs,v);
        else {lm_error("unknown or out-of-order option '%s'",k);return 0;}
    }
    if (o->msvc) {
        if (!o->compiler_selected) set_string(&o->compiler,"cl");
        if (!o->linker_selected) set_string(&o->linker,"link");
    }
    if (o->interactive) {
        char *v=prompt("Project",o->project);set_string(&o->project,v);free(v);
        v=prompt("Target executable",o->target?o->target:o->project);
        set_string(&o->target,v);free(v);
        v=prompt("Comma-separated C source files","main.c");
        {char *p=strtok(v,",");while (p) {p=lm_trim(p);if (*p) lm_add(&o->sources,p);p=strtok(NULL,",");}}
        free(v);
        v=prompt("Output folder",o->output_dir);set_string(&o->output_dir,v);free(v);
    }
    if (!o->target) {
        size_t n=strlen(o->project)+strlen(LM_DEFAULT_EXE)+1;
        o->target=(char *)malloc(n);if (!o->target) exit(2);
        snprintf(o->target,n,"%s%s",o->project,LM_DEFAULT_EXE);
    }
    if (!o->sources.len) {lm_error("provide at least one --source FILE");return 0;}
    if (!safe_path(o->target)||!safe_path(o->output_dir)||!safe_path(o->file)) {
        lm_error("paths cannot contain quote characters or newlines");return 0;
    }
    if (strchr(o->file,'/')||strchr(o->file,'\\')||!strcmp(o->file,".")||!strcmp(o->file,"..")) {
        lm_error("--output must be a filename in the current directory");return 0;
    }
    if (!strcmp(o->output_dir,".")||!strcmp(o->output_dir,"./")||!strcmp(o->output_dir,".\\")) {
        lm_error("OutputDir cannot be current directory");return 0;
    }
    {size_t j;for (j=0;j<o->sources.len;j++) if (!safe_path(o->sources.items[j])) {
        lm_error("unsafe source path: %s",o->sources.items[j]);return 0;
    }}
    return 1;
}
static void flags(FILE *f,const LMStrings *v) {
    size_t i;for (i=0;i<v->len;i++) fprintf(f," %s",v->items[i]);
}
static int generate(const Options *o) {
    FILE *f;size_t i;char **objects;
    if (lm_isfile(o->file) && !o->overwrite) {
        lm_error("'%s' exists (use --force to overwrite)",o->file);return 0;
    }
    f=fopen(o->file,"wb");if (!f) {lm_error("cannot write '%s'",o->file);return 0;}
    fprintf(f,"# LikeMakefile generated by LikeMakefileBuilder\n"
              "# Project: %s\n\n"
              "Defaults:\nCOMPILER %s\nLINKER %s\n\n"
              "OutputDir:\n%s\n\n"
              "DependsOn:\n",o->project,o->compiler,o->linker,o->output_dir);
    for (i=0;i<o->sources.len;i++) fprintf(f,"%s\n",o->sources.items[i]);
    fprintf(f,"\nExecution:build\n");
    objects=(char **)calloc(o->sources.len,sizeof(*objects));if (!objects) exit(2);
    for (i=0;i<o->sources.len;i++) {
        char *src=quoted(o->sources.items[i]),*obj;
        objects[i]=object_name(o->sources.items[i],o->msvc);obj=quoted(objects[i]);
        if (o->msvc) {
            fprintf(f,"COMPILER /nologo /c %s /Fo%s",src,obj);
        } else {
            fprintf(f,"COMPILER -c %s -o %s",src,obj);
        }
        flags(f,&o->cflags);fprintf(f,"\n");free(src);free(obj);
    }
    fprintf(f,"LINKER");
    for (i=0;i<o->sources.len;i++) {
        char *obj=quoted(objects[i]);fprintf(f," %s",obj);free(obj);
    }
    {char *target=quoted(o->target);
        if (o->msvc) fprintf(f," /OUT:%s",target);
        else fprintf(f," -o %s",target);
        free(target);
    }
    flags(f,&o->ldflags);fprintf(f,"\n\nExecution:clean\n@clean\n");
    for (i=0;i<o->len;i++) {
        size_t j;const Extra *x=&o->extras[i];
        if (!strcmp(x->name,"build")||!strcmp(x->name,"clean")||
            !strcmp(x->name,"Defaults")||!strcmp(x->name,"Config")||
            !strcmp(x->name,"MainConfig")||!strcmp(x->name,"DependsOn")||
            !strcmp(x->name,"OutputDir")) {
            lm_error("reserved/duplicate section name '%s'",x->name);fclose(f);return 0;
        }
        fprintf(f,"\n");
        if (x->depends.len) {
            fprintf(f,"DependsOn:\n");
            for (j=0;j<x->depends.len;j++) fprintf(f,"%s\n",x->depends.items[j]);
            fprintf(f,"\n");
        }
        if (x->configs.len) {
            fprintf(f,"Config:\n");
            for (j=0;j<x->configs.len;j++) fprintf(f,"%s\n",x->configs.items[j]);
            fprintf(f,"\n");
        }
        fprintf(f,"Execution:%s\n",x->name);
        for (j=0;j<x->commands.len;j++) fprintf(f,"%s\n",x->commands.items[j]);
    }
    for (i=0;i<o->sources.len;i++) free(objects[i]);
    free(objects);
    if (fclose(f)!=0) {lm_error("could not finalize '%s'",o->file);return 0;}
    printf("Generated %s (%zu sources, %zu custom sections)\n",o->file,o->sources.len,o->len);
    return 1;
}
static void cleanup(Options *o) {
    size_t i;free(o->project);free(o->target);free(o->file);
    free(o->output_dir);free(o->compiler);free(o->linker);
    lm_free(&o->sources);lm_free(&o->cflags);lm_free(&o->ldflags);
    for (i=0;i<o->len;i++) {free(o->extras[i].name);lm_free(&o->extras[i].commands);
        lm_free(&o->extras[i].depends);lm_free(&o->extras[i].configs);}
    free(o->extras);
}
int main(int argc,char **argv) {
    Options o={0};int rc=parse(&o,argc,argv),success=0;
    if (rc>0) success=generate(&o);
    cleanup(&o);return rc<0?0:(success?0:2);
}
