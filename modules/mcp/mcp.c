/*
 * curry_mcp — MCP (Model Context Protocol) server module for Curry Scheme.
 *
 * Two transports are supported:
 *
 *   stdio  — newline-delimited JSON-RPC 2.0 over stdin/stdout (one client).
 *            Activated by (mcp-serve) or (mcp-serve name version).
 *
 *   SSE    — HTTP + Server-Sent Events, multiple concurrent clients.
 *            Activated by (mcp-serve-sse port [name [version]]).
 *            GET /sse  → opens SSE stream, emits endpoint URL with sessionId.
 *            POST /message?sessionId=X → receives JSON-RPC, response via SSE.
 *
 * Tool and resource handlers are plain Scheme lambdas registered before
 * calling mcp-serve / mcp-serve-sse.  Boehm GC keeps handler procs and
 * schemas alive via static globals.
 *
 * Scheme API:
 *   (mcp-tool name description schema handler)    — register a callable tool
 *   (mcp-resource uri description handler)        — register a browseable resource
 *   (mcp-text string)                             — return text from a handler
 *   (mcp-json value)                              — return any Scheme value as JSON
 *   (mcp-notify-progress current total message)   — emit a progress notification
 *   (mcp-serve)                                   — stdio, blocks until EOF
 *   (mcp-serve server-name version)               — stdio with custom identity
 *   (mcp-serve-sse port)                          — SSE on port, blocks forever
 *   (mcp-serve-sse port server-name)              — SSE with custom name
 *   (mcp-serve-sse port server-name version)      — SSE with custom name+version
 *
 * Schema format — alist mapping param names to property alists:
 *   '((query . ((type . "string") (description . "Search terms")))
 *     (limit . ((type . "integer") (description . "Max results") (default . 10))))
 * Parameters without a (default . ...) are marked required in the JSON Schema.
 */

#include <curry.h>
#include "eval.h"    /* SCM_PROTECT, ExnHandler, current_handler */
#include "object.h"  /* ErrorObj, vis_error, as_err */
#include "gc.h"      /* gc_register_thread */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef _WIN32
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <signal.h>
#  define HAVE_SSE 1
#endif

#define MCP_VERSION   "2024-11-05"
#define MAX_TOOLS      64
#define MAX_RESOURCES  32
#define MAX_SESSIONS   32
#define LINE_LIMIT     (1 << 20)   /* 1 MB per JSON-RPC line */

/* ---- Forward declarations ---- */

static curry_val parse_val(const char **p);
static void      dispatch(curry_val req);

/* ---- Tool / resource registry ---- */

typedef struct {
    char      name[128];
    char      desc[512];
    curry_val schema;
    curry_val handler;
} Tool;

typedef struct {
    char      uri[256];
    char      desc[256];
    curry_val handler;
} Resource;

static Tool     s_tools[MAX_TOOLS];
static int      s_ntool = 0;
static Resource s_res[MAX_RESOURCES];
static int      s_nres  = 0;

static char s_server_name[128] = "curry-mcp";
static char s_server_ver[32]   = "0.7.2";

/* Thread-local: request-id of the current tool call, for mcp-notify-progress.
 * Thread-local so concurrent SSE sessions don't clobber each other. */
static _Thread_local char s_cur_token[64] = "";

/* Serialises curry_apply() — the Scheme evaluator is not re-entrant. */
static pthread_mutex_t s_dispatch_lock = PTHREAD_MUTEX_INITIALIZER;

/* Protects stdout writes in stdio mode. */
static pthread_mutex_t s_out_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- SSE session table ---- */

#ifdef HAVE_SSE

typedef struct {
    char            id[37];   /* UUID */
    int             fd;       /* SSE socket owned by the GET /sse thread */
    bool            active;
    pthread_mutex_t wlock;    /* serialises writes to fd */
} Session;

static Session          s_sessions[MAX_SESSIONS];
static pthread_rwlock_t s_sess_rwlock = PTHREAD_RWLOCK_INITIALIZER;
static bool             s_sess_inited = false;

/* Thread-local: non-NULL while a tool call is dispatched via SSE. */
static _Thread_local Session *t_sse_session = NULL;

static void ensure_sess_init(void) {
    if (s_sess_inited) return;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        s_sessions[i].active = false;
        s_sessions[i].fd     = -1;
        pthread_mutex_init(&s_sessions[i].wlock, NULL);
    }
    s_sess_inited = true;
}

static void gen_uuid(char out[37]) {
    uint8_t b[16];
    int rfd = open("/dev/urandom", O_RDONLY);
    if (rfd >= 0) { (void)read(rfd, b, 16); close(rfd); }
    else { for (int i = 0; i < 16; i++) b[i] = (uint8_t)rand(); }
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    snprintf(out, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],
        b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
}

static Session *alloc_session(const char *uuid, int fd) {
    pthread_rwlock_wrlock(&s_sess_rwlock);
    Session *found = NULL;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!s_sessions[i].active) {
            found = &s_sessions[i];
            strncpy(found->id, uuid, 36);
            found->id[36] = '\0';
            found->fd     = fd;
            found->active = true;
            break;
        }
    }
    pthread_rwlock_unlock(&s_sess_rwlock);
    return found;
}

static Session *find_session(const char *uuid) {
    pthread_rwlock_rdlock(&s_sess_rwlock);
    Session *found = NULL;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active && strcmp(s_sessions[i].id, uuid) == 0) {
            found = &s_sessions[i];
            break;
        }
    }
    pthread_rwlock_unlock(&s_sess_rwlock);
    return found;
}

/* Write one SSE data frame to the session.  Checks active under wlock. */
static void sse_send(Session *sess, const char *json) {
    size_t jlen  = strlen(json);
    size_t flen  = jlen + 8;        /* "data: " + json + "\n\n" */
    char  *frame = malloc(flen + 1);
    int    n     = snprintf(frame, flen + 1, "data: %s\n\n", json);
    pthread_mutex_lock(&sess->wlock);
    if (sess->active) {
        if (send(sess->fd, frame, (size_t)n, MSG_NOSIGNAL) <= 0)
            sess->active = false;
    }
    pthread_mutex_unlock(&sess->wlock);
    free(frame);
}

#endif /* HAVE_SSE */

/* ---- String builder ---- */

typedef struct { char *buf; size_t len; size_t cap; } SB;

static void sb_init(SB *b) { b->cap=256; b->len=0; b->buf=malloc(b->cap); b->buf[0]='\0'; }
static void sb_free(SB *b) { free(b->buf); }
static void sb_grow(SB *b, size_t n) {
    while (b->len+n+1 >= b->cap) { b->cap*=2; b->buf=realloc(b->buf,b->cap); }
}
static void sb_c(SB *b, char c)        { sb_grow(b,1); b->buf[b->len++]=c; b->buf[b->len]='\0'; }
static void sb_s(SB *b, const char *s) { while(*s) sb_c(b,*s++); }
static void sb_n(SB *b, long n)        { char t[32]; snprintf(t,sizeof(t),"%ld",n); sb_s(b,t); }
static void sb_f(SB *b, double d)      { char t[32]; snprintf(t,sizeof(t),"%g",d); sb_s(b,t); }

static void sb_quoted(SB *b, const char *s) {
    sb_c(b,'"');
    for (; *s; s++) {
        if      (*s=='"')  sb_s(b,"\\\"");
        else if (*s=='\\') sb_s(b,"\\\\");
        else if (*s=='\n') sb_s(b,"\\n");
        else if (*s=='\r') sb_s(b,"\\r");
        else if (*s=='\t') sb_s(b,"\\t");
        else               sb_c(b,*s);
    }
    sb_c(b,'"');
}

static void sb_val(SB *b, curry_val v) {
    if (curry_is_nil(v)||curry_is_void(v)) { sb_s(b,"null"); return; }
    if (curry_is_bool(v))   { sb_s(b,curry_bool(v)?"true":"false"); return; }
    if (curry_is_fixnum(v)) { sb_n(b,curry_fixnum(v)); return; }
    if (curry_is_float(v))  { sb_f(b,curry_float(v));  return; }
    if (curry_is_string(v)) { sb_quoted(b,curry_string(v)); return; }
    if (curry_is_symbol(v)) { sb_quoted(b,curry_symbol(v)); return; }
    if (curry_is_pair(v)) {
        if (curry_is_pair(curry_car(v))) {
            sb_c(b,'{');
            bool first=true;
            while (curry_is_pair(v)) {
                curry_val kv=curry_car(v); v=curry_cdr(v);
                if (!curry_is_pair(kv)) continue;
                if (!first) sb_c(b,',');
                first=false;
                curry_val k=curry_car(kv);
                if      (curry_is_string(k)) sb_quoted(b,curry_string(k));
                else if (curry_is_symbol(k)) sb_quoted(b,curry_symbol(k));
                else                         sb_s(b,"\"?\"");
                sb_c(b,':'); sb_val(b,curry_cdr(kv));
            }
            sb_c(b,'}');
        } else {
            sb_c(b,'[');
            bool first=true;
            while (curry_is_pair(v)) {
                if (!first) sb_c(b,',');
                first=false;
                sb_val(b,curry_car(v)); v=curry_cdr(v);
            }
            sb_c(b,']');
        }
        return;
    }
    sb_s(b,"null");
}

/* ---- Output — routes to SSE session or stdout ---- */

static void emit_line(const char *s) {
#ifdef HAVE_SSE
    if (t_sse_session) {
        sse_send(t_sse_session, s);
        return;
    }
#endif
    pthread_mutex_lock(&s_out_lock);
    fputs(s, stdout); fputc('\n', stdout); fflush(stdout);
    pthread_mutex_unlock(&s_out_lock);
}

static void emit_result(const char *id, const char *result) {
    SB b; sb_init(&b);
    sb_s(&b,"{\"jsonrpc\":\"2.0\",\"id\":"); sb_s(&b,id);
    sb_s(&b,",\"result\":"); sb_s(&b,result); sb_s(&b,"}");
    emit_line(b.buf); sb_free(&b);
}

static void emit_error(const char *id, int code, const char *msg) {
    SB b; sb_init(&b);
    sb_s(&b,"{\"jsonrpc\":\"2.0\",\"id\":"); sb_s(&b,id);
    sb_s(&b,",\"error\":{\"code\":"); sb_n(&b,code);
    sb_s(&b,",\"message\":"); sb_quoted(&b,msg); sb_s(&b,"}}");
    emit_line(b.buf); sb_free(&b);
}

/* ---- JSON parser ---- */

static void skip_ws(const char **p) { while(**p && (unsigned char)**p<=' ') (*p)++; }

static curry_val parse_str(const char **p) {
    (*p)++;
    size_t cap=128, len=0; char *buf=malloc(cap);
    while (**p && **p!='"') {
        char c=*(*p)++;
        if (c=='\\') {
            c=*(*p)++;
            switch(c) { case 'n':c='\n';break; case 't':c='\t';break;
                         case 'r':c='\r';break; default:break; }
        }
        buf[len++]=c;
        if (len+2>=cap) { cap*=2; buf=realloc(buf,cap); }
    }
    if (**p=='"') (*p)++;
    buf[len]='\0';
    curry_val r=curry_make_string(buf); free(buf); return r;
}

static curry_val parse_val(const char **p) {
    skip_ws(p);
    if (!**p) return curry_nil();
    if (**p=='"') return parse_str(p);
    if (**p=='{') {
        (*p)++;
        curry_val al=curry_nil(); skip_ws(p);
        if (**p=='}') { (*p)++; return al; }
        while (**p) {
            skip_ws(p); if (**p!='"') break;
            curry_val k=parse_str(p); skip_ws(p);
            if (**p==':') (*p)++;
            curry_val v=parse_val(p);
            al=curry_make_pair(curry_make_pair(k,v),al);
            skip_ws(p);
            if (**p==',') { (*p)++; continue; }
            if (**p=='}') { (*p)++; break; }
        }
        return al;
    }
    if (**p=='[') {
        (*p)++;
        curry_val elems[1024]; int n=0; skip_ws(p);
        if (**p==']') { (*p)++; return curry_nil(); }
        while (**p && n<1024) {
            elems[n++]=parse_val(p); skip_ws(p);
            if (**p==',') { (*p)++; continue; }
            if (**p==']') { (*p)++; break; }
        }
        curry_val lst=curry_nil();
        for (int i=n-1;i>=0;i--) lst=curry_make_pair(elems[i],lst);
        return lst;
    }
    if (strncmp(*p,"true",4)==0)  { *p+=4; return curry_make_bool(true); }
    if (strncmp(*p,"false",5)==0) { *p+=5; return curry_make_bool(false); }
    if (strncmp(*p,"null",4)==0)  { *p+=4; return curry_nil(); }
    char num[64]; int i=0; bool fp=false;
    /* Write one character into num[], advancing *p; silently clamps at 63. */
    #define NUMW(c) do { if (i < (int)sizeof(num)-1) num[i++]=(c); } while(0)
    if (**p=='-') NUMW(*(*p)++);
    while (isdigit((unsigned char)**p)) NUMW(*(*p)++);
    if (**p=='.') {
        fp=true; NUMW(*(*p)++);
        while(isdigit((unsigned char)**p)) NUMW(*(*p)++);
    }
    if (**p=='e'||**p=='E') {
        fp=true; NUMW(*(*p)++);
        if (**p=='+'||**p=='-') NUMW(*(*p)++);
        while (isdigit((unsigned char)**p)) NUMW(*(*p)++);
    }
    num[i]='\0';
    #undef NUMW
    return fp ? curry_make_float(atof(num)) : curry_make_fixnum(atol(num));
}

static curry_val aget(curry_val al, const char *key) {
    while (curry_is_pair(al)) {
        curry_val kv=curry_car(al); al=curry_cdr(al);
        if (!curry_is_pair(kv)) continue;
        curry_val k=curry_car(kv);
        const char *ks = curry_is_string(k) ? curry_string(k)
                       : curry_is_symbol(k) ? curry_symbol(k) : NULL;
        if (ks && strcmp(ks,key)==0) return curry_cdr(kv);
    }
    return curry_nil();
}

/* ---- Schema serializer ---- */

static void sb_schema(SB *b, curry_val schema) {
    sb_s(b,"{\"type\":\"object\",\"properties\":{");
    curry_val req=curry_nil(); bool first=true;
    for (curry_val p=schema; curry_is_pair(p); p=curry_cdr(p)) {
        curry_val entry=curry_car(p);
        if (!curry_is_pair(entry)) continue;
        curry_val name=curry_car(entry), props=curry_cdr(entry);
        const char *nm = curry_is_symbol(name) ? curry_symbol(name)
                       : curry_is_string(name)  ? curry_string(name) : "?";
        if (!first) sb_c(b,',');
        first=false;
        sb_quoted(b,nm); sb_c(b,':'); sb_c(b,'{');
        curry_val type=aget(props,"type"), desc=aget(props,"description"), def=aget(props,"default");
        bool has_def=!curry_is_nil(def), need_comma=false;
        if (curry_is_string(type)) {
            sb_s(b,"\"type\":"); sb_quoted(b,curry_string(type)); need_comma=true;
        }
        if (curry_is_string(desc)) {
            if (need_comma) sb_c(b,',');
            sb_s(b,"\"description\":"); sb_quoted(b,curry_string(desc)); need_comma=true;
        }
        if (has_def) {
            if (need_comma) sb_c(b,',');
            sb_s(b,"\"default\":"); sb_val(b,def);
        } else {
            req=curry_make_pair(name,req);
        }
        sb_c(b,'}');
    }
    sb_s(b,"}");
    if (curry_is_pair(req)) {
        sb_s(b,",\"required\":["); bool f2=true;
        while (curry_is_pair(req)) {
            curry_val n=curry_car(req); req=curry_cdr(req);
            if (!f2) sb_c(b,',');
            f2=false;
            const char *nm = curry_is_symbol(n) ? curry_symbol(n)
                           : curry_is_string(n)  ? curry_string(n) : "?";
            sb_quoted(b,nm);
        }
        sb_c(b,']');
    }
    sb_c(b,'}');
}

/* ---- Protocol handlers ---- */

static void handle_initialize(const char *id) {
    SB r; sb_init(&r);
    sb_s(&r,"{\"protocolVersion\":\"" MCP_VERSION "\","
           "\"capabilities\":{\"tools\":{},\"resources\":{}},"
           "\"serverInfo\":{\"name\":"); sb_quoted(&r,s_server_name);
    sb_s(&r,",\"version\":"); sb_quoted(&r,s_server_ver); sb_s(&r,"}}");
    emit_result(id,r.buf); sb_free(&r);
}

static void handle_tools_list(const char *id) {
    SB r; sb_init(&r);
    sb_s(&r,"{\"tools\":[");
    for (int i=0; i<s_ntool; i++) {
        if (i) sb_c(&r,',');
        sb_s(&r,"{\"name\":"); sb_quoted(&r,s_tools[i].name);
        sb_s(&r,",\"description\":"); sb_quoted(&r,s_tools[i].desc);
        sb_s(&r,",\"inputSchema\":"); sb_schema(&r,s_tools[i].schema);
        sb_c(&r,'}');
    }
    sb_s(&r,"]}");
    emit_result(id,r.buf); sb_free(&r);
}

static curry_val json_args_to_alist(curry_val jargs) {
    curry_val out=curry_nil();
    while (curry_is_pair(jargs)) {
        curry_val kv=curry_car(jargs); jargs=curry_cdr(jargs);
        if (!curry_is_pair(kv)) continue;
        curry_val k=curry_car(kv);
        const char *ks=curry_is_string(k)?curry_string(k):curry_symbol(k);
        out=curry_make_pair(curry_make_pair(curry_make_symbol(ks),curry_cdr(kv)),out);
    }
    return out;
}

static void exn_msg(curry_val exn, char *buf, size_t cap) {
    if (curry_is_string(exn))   { snprintf(buf,cap,"%s",curry_string(exn)); return; }
    if (vis_error(exn))         { snprintf(buf,cap,"%s",curry_string(as_err(exn)->message)); return; }
    if (curry_is_pair(exn) && curry_is_string(curry_cdr(exn)))
                                { snprintf(buf,cap,"%s",curry_string(curry_cdr(exn))); return; }
    snprintf(buf,cap,"error");
}

static void sb_content_text(SB *b, curry_val result) {
    sb_s(b,"{\"type\":\"text\",\"text\":");
    if (curry_is_pair(result) && curry_is_symbol(curry_car(result))) {
        const char *tag=curry_symbol(curry_car(result));
        curry_val   val=curry_cdr(result);
        if (strcmp(tag,"mcp-text")==0 && curry_is_string(val)) {
            sb_quoted(b,curry_string(val));
        } else if (strcmp(tag,"mcp-json")==0) {
            SB inner; sb_init(&inner); sb_val(&inner,val); sb_quoted(b,inner.buf); sb_free(&inner);
        } else {
            SB inner; sb_init(&inner); sb_val(&inner,result); sb_quoted(b,inner.buf); sb_free(&inner);
        }
    } else if (curry_is_string(result)) {
        sb_quoted(b,curry_string(result));
    } else {
        SB inner; sb_init(&inner); sb_val(&inner,result); sb_quoted(b,inner.buf); sb_free(&inner);
    }
    sb_c(b,'}');
}

static void handle_tools_call(const char *id, curry_val params) {
    curry_val name_v=aget(params,"name");
    if (!curry_is_string(name_v)) { emit_error(id,-32602,"tools/call: missing name"); return; }
    Tool *tool=NULL;
    for (int i=0; i<s_ntool; i++)
        if (strcmp(s_tools[i].name,curry_string(name_v))==0) { tool=&s_tools[i]; break; }
    if (!tool) { emit_error(id,-32602,"unknown tool"); return; }
    strncpy(s_cur_token,id,sizeof(s_cur_token)-1);
    curry_val jargs=aget(params,"arguments");
    curry_val args=json_args_to_alist(curry_is_pair(jargs)?jargs:curry_nil());
    curry_val result=curry_void();
    char errbuf[512]=""; bool failed=false;
    ExnHandler h;
    SCM_PROTECT(h,
        { curry_val av[1]={args}; result=curry_apply(tool->handler,1,av); },
        { failed=true; exn_msg(h.exn,errbuf,sizeof(errbuf)); }
    );
    s_cur_token[0]='\0';
    if (failed) { emit_error(id,-32603,errbuf); return; }
    SB r; sb_init(&r);
    sb_s(&r,"{\"content\":["); sb_content_text(&r,result); sb_s(&r,"]}");
    emit_result(id,r.buf); sb_free(&r);
}

static void handle_resources_list(const char *id) {
    SB r; sb_init(&r);
    sb_s(&r,"{\"resources\":[");
    for (int i=0; i<s_nres; i++) {
        if (i) sb_c(&r,',');
        sb_s(&r,"{\"uri\":"); sb_quoted(&r,s_res[i].uri);
        const char *nm=strrchr(s_res[i].uri,'/');
        sb_s(&r,",\"name\":"); sb_quoted(&r,nm?nm+1:s_res[i].uri);
        sb_s(&r,",\"description\":"); sb_quoted(&r,s_res[i].desc);
        sb_c(&r,'}');
    }
    sb_s(&r,"]}");
    emit_result(id,r.buf); sb_free(&r);
}

static void handle_resources_read(const char *id, curry_val params) {
    curry_val uri_v=aget(params,"uri");
    if (!curry_is_string(uri_v)) { emit_error(id,-32602,"resources/read: missing uri"); return; }
    Resource *res=NULL;
    for (int i=0;i<s_nres;i++)
        if (strcmp(s_res[i].uri,curry_string(uri_v))==0) { res=&s_res[i]; break; }
    if (!res) { emit_error(id,-32602,"unknown resource"); return; }
    curry_val result=curry_void();
    char errbuf[512]=""; bool failed=false;
    ExnHandler h;
    SCM_PROTECT(h,
        { curry_val av[1]={uri_v}; result=curry_apply(res->handler,1,av); },
        { failed=true; exn_msg(h.exn,errbuf,sizeof(errbuf)); }
    );
    if (failed) { emit_error(id,-32603,errbuf); return; }
    SB r; sb_init(&r);
    sb_s(&r,"{\"contents\":[{\"uri\":"); sb_quoted(&r,curry_string(uri_v));
    sb_s(&r,",\"mimeType\":\"text/plain\",\"text\":");
    if (curry_is_pair(result) && curry_is_symbol(curry_car(result)) &&
        strcmp(curry_symbol(curry_car(result)),"mcp-text")==0) {
        sb_quoted(&r,curry_string(curry_cdr(result)));
    } else if (curry_is_string(result)) {
        sb_quoted(&r,curry_string(result));
    } else {
        SB inner; sb_init(&inner); sb_val(&inner,result); sb_quoted(&r,inner.buf); sb_free(&inner);
    }
    sb_s(&r,"}]}");
    emit_result(id,r.buf); sb_free(&r);
}

/* ---- Request dispatch ---- */

static void dispatch(curry_val req) {
    curry_val method_v=aget(req,"method");
    if (!curry_is_string(method_v)) return;
    const char *method=curry_string(method_v);
    curry_val id_v=aget(req,"id");
    char id_buf[64]="null";
    if (curry_is_fixnum(id_v)) {
        snprintf(id_buf,sizeof(id_buf),"%ld",curry_fixnum(id_v));
    } else if (curry_is_string(id_v)) {
        SB tmp; sb_init(&tmp); sb_quoted(&tmp,curry_string(id_v));
        strncpy(id_buf,tmp.buf,sizeof(id_buf)-1); sb_free(&tmp);
    }
    curry_val params=aget(req,"params");
    if (!curry_is_pair(params)) params=curry_nil();
    if      (strcmp(method,"initialize")==0)       handle_initialize(id_buf);
    else if (strcmp(method,"tools/list")==0)        handle_tools_list(id_buf);
    else if (strcmp(method,"tools/call")==0)        handle_tools_call(id_buf,params);
    else if (strcmp(method,"resources/list")==0)    handle_resources_list(id_buf);
    else if (strcmp(method,"resources/read")==0)    handle_resources_read(id_buf,params);
    else if (strncmp(method,"notifications/",14)==0) { /* client notification — no response */ }
    else if (strcmp(id_buf,"null")!=0)              emit_error(id_buf,-32601,"method not found");
}

/* ---- Scheme primitives ---- */

static curry_val fn_mcp_tool(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (s_ntool>=MAX_TOOLS)         curry_error("mcp-tool: registry full (max %d)",MAX_TOOLS);
    if (!curry_is_string(av[0]))    curry_error("mcp-tool: name must be a string");
    if (!curry_is_string(av[1]))    curry_error("mcp-tool: description must be a string");
    if (!curry_is_procedure(av[3])) curry_error("mcp-tool: handler must be a procedure");
    Tool *t=&s_tools[s_ntool++];
    strncpy(t->name,curry_string(av[0]),sizeof(t->name)-1);
    strncpy(t->desc,curry_string(av[1]),sizeof(t->desc)-1);
    t->schema=av[2];
    t->handler=av[3];
    return curry_void();
}

static curry_val fn_mcp_resource(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (s_nres>=MAX_RESOURCES)      curry_error("mcp-resource: registry full (max %d)",MAX_RESOURCES);
    if (!curry_is_string(av[0]))    curry_error("mcp-resource: uri must be a string");
    if (!curry_is_string(av[1]))    curry_error("mcp-resource: description must be a string");
    if (!curry_is_procedure(av[2])) curry_error("mcp-resource: handler must be a procedure");
    Resource *r=&s_res[s_nres++];
    strncpy(r->uri, curry_string(av[0]),sizeof(r->uri)-1);
    strncpy(r->desc,curry_string(av[1]),sizeof(r->desc)-1);
    r->handler=av[2];
    return curry_void();
}

static curry_val fn_mcp_text(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_string(av[0])) curry_error("mcp-text: expected string");
    return curry_make_pair(curry_make_symbol("mcp-text"),av[0]);
}

static curry_val fn_mcp_json(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    return curry_make_pair(curry_make_symbol("mcp-json"),av[0]);
}

static curry_val fn_mcp_progress(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (s_cur_token[0]=='\0') return curry_void();
    double cur=curry_is_fixnum(av[0])?(double)curry_fixnum(av[0]):curry_float(av[0]);
    double tot=curry_is_fixnum(av[1])?(double)curry_fixnum(av[1]):curry_float(av[1]);
    const char *msg=curry_is_string(av[2])?curry_string(av[2]):"";
    SB b; sb_init(&b);
    sb_s(&b,"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\","
           "\"params\":{\"progressToken\":"); sb_quoted(&b,s_cur_token);
    sb_s(&b,",\"value\":{\"kind\":\"report\",\"percentage\":");
    sb_f(&b,tot>0?100.0*cur/tot:0.0);
    sb_s(&b,",\"message\":"); sb_quoted(&b,msg); sb_s(&b,"}}}");
    emit_line(b.buf); sb_free(&b);
    return curry_void();
}

/* (mcp-serve) or (mcp-serve name version) — stdio transport */
static curry_val fn_mcp_serve(int ac, curry_val *av, void *ud) {
    (void)ud;
    if (ac>=1 && curry_is_string(av[0])) strncpy(s_server_name,curry_string(av[0]),sizeof(s_server_name)-1);
    if (ac>=2 && curry_is_string(av[1])) strncpy(s_server_ver, curry_string(av[1]),sizeof(s_server_ver)-1);
    char *line=malloc(LINE_LIMIT);
    if (!line) curry_error("mcp-serve: out of memory");
    while (fgets(line,LINE_LIMIT,stdin)) {
        size_t n=strlen(line);
        while (n>0 && (line[n-1]=='\n'||line[n-1]=='\r')) line[--n]='\0';
        if (n==0) continue;
        const char *p=line;
        curry_val req=parse_val(&p);
        if (curry_is_pair(req)) dispatch(req);
    }
    free(line);
    return curry_void();
}

/* ---- SSE transport ---- */

#ifdef HAVE_SSE

/* Read one line from fd into buf (strips \r\n).  Returns bytes read, 0 on
 * blank line, -1 on error/EOF. */
static ssize_t fd_read_line(int fd, char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0) return -1;
        if (c == '\r') continue;
        if (c == '\n') break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    return (ssize_t)n;
}

/* Read exactly n bytes from fd into buf (NUL-terminated). */
static bool fd_read_exact(int fd, char *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    buf[n] = '\0';
    return true;
}

typedef struct {
    char   method[16];
    char   path[512];
    char   query[256];
    char  *body;        /* malloc'd, may be NULL */
    size_t body_len;
} HttpReq;

/* Parse one HTTP/1.x request from fd.  Returns false on connection error. */
static bool http_recv(int fd, HttpReq *req) {
    memset(req, 0, sizeof(*req));
    char line[2048];

    /* Request line */
    if (fd_read_line(fd, line, sizeof(line)) < 0) return false;
    char *sp1 = strchr(line, ' ');
    if (!sp1) return false;
    *sp1 = '\0';
    strncpy(req->method, line, sizeof(req->method) - 1);
    char *path_start = sp1 + 1;
    char *sp2 = strchr(path_start, ' ');
    if (sp2) *sp2 = '\0';
    char *q = strchr(path_start, '?');
    if (q) {
        strncpy(req->query, q + 1, sizeof(req->query) - 1);
        *q = '\0';
    }
    strncpy(req->path, path_start, sizeof(req->path) - 1);

    /* Headers */
    size_t content_length = 0;
    ssize_t r;
    while ((r = fd_read_line(fd, line, sizeof(line))) > 0) {
        if (strncasecmp(line, "content-length:", 15) == 0)
            content_length = (size_t)atol(line + 15);
    }
    if (r < 0) return false; /* connection dropped mid-headers */

    /* Body */
    if (content_length > 0 && content_length < (size_t)LINE_LIMIT) {
        req->body = malloc(content_length + 1);
        if (!req->body) return false;
        if (!fd_read_exact(fd, req->body, content_length)) {
            free(req->body); req->body = NULL; return false;
        }
        req->body_len = content_length;
    }
    return true;
}

static const char *SSE_HEADERS =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/event-stream\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: keep-alive\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "\r\n";

static const char *ACCEPTED =
    "HTTP/1.1 202 Accepted\r\n"
    "Content-Length: 0\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "\r\n";

static const char *CORS_PREFLIGHT =
    "HTTP/1.1 204 No Content\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static const char *NOT_FOUND =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static const char *SERVICE_UNAVAILABLE =
    "HTTP/1.1 503 Service Unavailable\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

/* Handle GET /sse — open an SSE stream and block until the client disconnects. */
static void handle_sse_get(int fd) {
    char uuid[37];
    gen_uuid(uuid);

    Session *sess = alloc_session(uuid, fd);
    if (!sess) {
        send(fd, SERVICE_UNAVAILABLE, strlen(SERVICE_UNAVAILABLE), MSG_NOSIGNAL);
        close(fd);
        return;
    }

    if (send(fd, SSE_HEADERS, strlen(SSE_HEADERS), MSG_NOSIGNAL) <= 0) goto done;

    /* Send the endpoint event so the client knows where to POST. */
    char endpoint[320];
    int  eplen = snprintf(endpoint, sizeof(endpoint),
        "event: endpoint\ndata: /message?sessionId=%s\n\n", uuid);
    if (send(fd, endpoint, (size_t)eplen, MSG_NOSIGNAL) <= 0) goto done;

    /* Keep the stream alive with periodic comment pings.  Exit when the client
     * disconnects (send fails) or when the session is marked inactive by a
     * concurrent write failure on the POST side. */
    while (true) {
        sleep(15);
        pthread_mutex_lock(&sess->wlock);
        bool alive = sess->active;
        if (alive) {
            if (send(fd, ": keepalive\n\n", 13, MSG_NOSIGNAL) <= 0) {
                sess->active = false;
                alive = false;
            }
        }
        pthread_mutex_unlock(&sess->wlock);
        if (!alive) break;
    }

done:
    pthread_mutex_lock(&sess->wlock);
    sess->active = false;
    sess->fd     = -1;
    pthread_mutex_unlock(&sess->wlock);
    close(fd);
}

/* Handle POST /message?sessionId=X — dispatch JSON-RPC and send response via SSE. */
static void handle_sse_post(int fd, HttpReq *req) {
    /* Extract sessionId from query string */
    char sess_id[37] = "";
    const char *sq = strstr(req->query, "sessionId=");
    if (sq) {
        sq += 10;
        size_t n = strcspn(sq, "&");
        if (n >= sizeof(sess_id)) n = sizeof(sess_id) - 1;
        strncpy(sess_id, sq, n);
        sess_id[n] = '\0';
    }

    /* Acknowledge immediately so the client isn't blocked on the HTTP response
     * while we run what could be a slow tool handler. */
    send(fd, ACCEPTED, strlen(ACCEPTED), MSG_NOSIGNAL);
    close(fd);

    if (!sess_id[0] || !req->body) return;

    Session *sess = find_session(sess_id);
    if (!sess) return;

    t_sse_session = sess;
    const char *p = req->body;
    curry_val rpc = parse_val(&p);
    if (curry_is_pair(rpc)) {
        pthread_mutex_lock(&s_dispatch_lock);
        dispatch(rpc);
        pthread_mutex_unlock(&s_dispatch_lock);
    }
    t_sse_session = NULL;
}

typedef struct { int fd; } ConnArg;

static void *conn_thread(void *arg) {
    ConnArg *ca = arg;
    int fd = ca->fd;
    free(ca);

    gc_register_thread();

    HttpReq req;
    if (!http_recv(fd, &req)) { close(fd); return NULL; }

    if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/sse") == 0) {
        handle_sse_get(fd);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/message") == 0) {
        handle_sse_post(fd, &req);
    } else if (strcmp(req.method, "OPTIONS") == 0) {
        send(fd, CORS_PREFLIGHT, strlen(CORS_PREFLIGHT), MSG_NOSIGNAL);
        close(fd);
    } else {
        send(fd, NOT_FOUND, strlen(NOT_FOUND), MSG_NOSIGNAL);
        close(fd);
    }

    free(req.body);
    return NULL;
}

/* (mcp-serve-sse port [name [version]]) — HTTP/SSE transport, blocks forever */
static curry_val fn_mcp_serve_sse(int ac, curry_val *av, void *ud) {
    (void)ud;
    if (ac < 1 || !curry_is_fixnum(av[0]))
        curry_error("mcp-serve-sse: first argument must be a port number");
    int port = (int)curry_fixnum(av[0]);
    if (ac >= 2 && curry_is_string(av[1]))
        strncpy(s_server_name, curry_string(av[1]), sizeof(s_server_name) - 1);
    if (ac >= 3 && curry_is_string(av[2]))
        strncpy(s_server_ver,  curry_string(av[2]), sizeof(s_server_ver) - 1);

    ensure_sess_init();
    signal(SIGPIPE, SIG_IGN); /* failed sends return -1 instead of killing the process */

    /* Try dual-stack IPv6 first, fall back to IPv4. */
    int srv = socket(AF_INET6, SOCK_STREAM, 0);
    if (srv >= 0) {
        int on = 1, off = 0;
        setsockopt(srv, SOL_SOCKET,  SO_REUSEADDR, &on,  sizeof(on));
        setsockopt(srv, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
        struct sockaddr_in6 addr = {0};
        addr.sin6_family = AF_INET6;
        addr.sin6_port   = htons((uint16_t)port);
        addr.sin6_addr   = in6addr_any;
        if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(srv); srv = -1;
        }
    }
    if (srv < 0) {
        srv = socket(AF_INET, SOCK_STREAM, 0);
        if (srv < 0) curry_error("mcp-serve-sse: socket() failed");
        int on = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        struct sockaddr_in addr = {0};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons((uint16_t)port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(srv);
            curry_error("mcp-serve-sse: bind() failed on port %d", port);
        }
    }
    if (listen(srv, 32) < 0) {
        close(srv);
        curry_error("mcp-serve-sse: listen() failed");
    }

    fprintf(stderr, "[mcp] SSE server listening on port %d\n", port);
    fflush(stderr);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    while (true) {
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) continue;
        ConnArg *ca = malloc(sizeof(ConnArg));
        ca->fd = fd;
        pthread_t tid;
        pthread_create(&tid, &attr, conn_thread, ca);
    }

    pthread_attr_destroy(&attr);
    close(srv);
    return curry_void();
}

#endif /* HAVE_SSE */

/* ---- Module entry point ---- */

void curry_module_init(CurryVM *vm) {
#define DEF(n,f,a,b) curry_define_fn(vm,n,f,a,b,NULL)
    DEF("mcp-tool",            fn_mcp_tool,     4, 4);
    DEF("mcp-resource",        fn_mcp_resource, 3, 3);
    DEF("mcp-text",            fn_mcp_text,     1, 1);
    DEF("mcp-json",            fn_mcp_json,     1, 1);
    DEF("mcp-notify-progress", fn_mcp_progress, 3, 3);
    DEF("mcp-serve",           fn_mcp_serve,    0, 2);
#ifdef HAVE_SSE
    DEF("mcp-serve-sse",       fn_mcp_serve_sse, 1, 3);
#endif
#undef DEF
}
