/*
 * curry_mcp — MCP (Model Context Protocol) server module for Curry Scheme.
 *
 * Transport: newline-delimited JSON-RPC 2.0 over stdio.
 * Tool and resource handlers are plain Scheme lambdas registered before
 * calling mcp-serve.  Tool calls run synchronously in the serve loop.
 * Boehm GC keeps handler procs and schemas alive via static globals.
 *
 * Scheme API:
 *   (mcp-tool name description schema handler)  — register a callable tool
 *   (mcp-resource uri description handler)       — register a browseable resource
 *   (mcp-text string)                            — return text from a handler
 *   (mcp-json value)                             — return any Scheme value as JSON
 *   (mcp-notify-progress current total message)  — emit a progress notification
 *   (mcp-serve)                                  — start serving (blocks until EOF)
 *   (mcp-serve server-name version)              — with custom server identity
 *
 * Schema format — alist mapping param names to property alists:
 *   '((query . ((type . "string") (description . "Search terms")))
 *     (limit . ((type . "integer") (description . "Max results") (default . 10))))
 * Parameters without a (default . ...) are marked required in the JSON Schema.
 *
 * Tool handlers receive an alist with symbol keys:
 *   (lambda (args)
 *     (let ((q (cdr (assq 'query args))))
 *       (mcp-text (search q))))
 */

#include <curry.h>
#include "eval.h"    /* SCM_PROTECT, ExnHandler, current_handler */
#include "object.h"  /* ErrorObj, vis_error, as_err */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MCP_VERSION  "2024-11-05"
#define MAX_TOOLS     64
#define MAX_RESOURCES 32
#define LINE_LIMIT    (1 << 20)   /* 1 MB per JSON-RPC line — covers large args */

/* ---- Registry ---- */

typedef struct {
    char      name[128];
    char      desc[512];
    curry_val schema;    /* Scheme alist; lives in static storage → GC finds it */
    curry_val handler;   /* Scheme procedure */
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
static char s_server_ver[32]   = "0.1.7";

/* Request id of the current tool call — used by mcp-notify-progress.
 * Only valid while a tool handler is running; empty string otherwise. */
static char s_cur_token[64] = "";

static pthread_mutex_t s_out_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- String builder ---- */

typedef struct { char *buf; size_t len; size_t cap; } SB;

static void sb_init(SB *b) { b->cap=256; b->len=0; b->buf=malloc(b->cap); b->buf[0]='\0'; }
static void sb_free(SB *b) { free(b->buf); }
static void sb_grow(SB *b, size_t n) {
    while (b->len+n+1 >= b->cap) { b->cap*=2; b->buf=realloc(b->buf,b->cap); }
}
static void sb_c(SB *b, char c)          { sb_grow(b,1); b->buf[b->len++]=c; b->buf[b->len]='\0'; }
static void sb_s(SB *b, const char *s)   { while(*s) sb_c(b,*s++); }
static void sb_n(SB *b, long n)          { char t[32]; snprintf(t,sizeof(t),"%ld",n); sb_s(b,t); }
static void sb_f(SB *b, double d)        { char t[32]; snprintf(t,sizeof(t),"%g",d); sb_s(b,t); }

/* Append s as a JSON string (with quotes and escaping). */
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

/* Serialize a Scheme value as JSON.
 * Pairs whose car is also a pair are treated as alists → JSON objects.
 * Other non-nil pairs are treated as lists → JSON arrays. */
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

/* ---- Output ---- */

static void emit_line(const char *s) {
    pthread_mutex_lock(&s_out_lock);
    fputs(s,stdout); fputc('\n',stdout); fflush(stdout);
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

static curry_val parse_val(const char **p);

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
    /* number */
    char num[64]; int i=0; bool fp=false;
    if (**p=='-') num[i++]=*(*p)++;
    while (isdigit((unsigned char)**p)) num[i++]=*(*p)++;
    if (**p=='.') { fp=true; num[i++]=*(*p)++; while(isdigit((unsigned char)**p)) num[i++]=*(*p)++; }
    if (**p=='e'||**p=='E') {
        fp=true; num[i++]=*(*p)++;
        if (**p=='+'||**p=='-') num[i++]=*(*p)++;
        while (isdigit((unsigned char)**p)) num[i++]=*(*p)++;
    }
    num[i]='\0';
    return fp ? curry_make_float(atof(num)) : curry_make_fixnum(atol(num));
}

/* Look up a string key in a parsed JSON alist. */
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

/* Convert the Scheme schema alist to a JSON Schema object.
 * Parameters lacking a (default . ...) entry are added to "required". */
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

/* Remap JSON args alist (string keys) to a Scheme alist (symbol keys). */
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

/* Extract a human-readable message from a Scheme exception value. */
static void exn_msg(curry_val exn, char *buf, size_t cap) {
    if (curry_is_string(exn))   { snprintf(buf,cap,"%s",curry_string(exn)); return; }
    if (vis_error(exn))         { snprintf(buf,cap,"%s",curry_string(as_err(exn)->message)); return; }
    if (curry_is_pair(exn) && curry_is_string(curry_cdr(exn)))
                                { snprintf(buf,cap,"%s",curry_string(curry_cdr(exn))); return; }
    snprintf(buf,cap,"error");
}

/* Format a tool/resource result value as a JSON content text item. */
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

    /* Reconstruct the id as a JSON fragment for use in responses. */
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

/* (mcp-tool "name" "description" schema-alist handler) */
static curry_val fn_mcp_tool(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (s_ntool>=MAX_TOOLS)         curry_error("mcp-tool: registry full (max %d)",MAX_TOOLS);
    if (!curry_is_string(av[0]))    curry_error("mcp-tool: name must be a string");
    if (!curry_is_string(av[1]))    curry_error("mcp-tool: description must be a string");
    if (!curry_is_procedure(av[3])) curry_error("mcp-tool: handler must be a procedure");
    Tool *t=&s_tools[s_ntool++];
    strncpy(t->name,curry_string(av[0]),sizeof(t->name)-1);
    strncpy(t->desc,curry_string(av[1]),sizeof(t->desc)-1);
    t->schema=av[2];    /* static storage → Boehm GC conservatively scans it */
    t->handler=av[3];
    return curry_void();
}

/* (mcp-resource "uri" "description" handler) */
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

/* (mcp-text "string") — tag a string as text content for the protocol layer. */
static curry_val fn_mcp_text(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_string(av[0])) curry_error("mcp-text: expected string");
    return curry_make_pair(curry_make_symbol("mcp-text"),av[0]);
}

/* (mcp-json value) — serialize any Scheme value as JSON text content. */
static curry_val fn_mcp_json(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    return curry_make_pair(curry_make_symbol("mcp-json"),av[0]);
}

/* (mcp-notify-progress current total message) — emit a progress notification.
 * Only has effect when called from within a tool handler during mcp-serve. */
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

/* (mcp-serve) or (mcp-serve server-name version) — run the protocol loop. */
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

/* ---- Module entry point ---- */

void curry_module_init(CurryVM *vm) {
#define DEF(n,f,a,b) curry_define_fn(vm,n,f,a,b,NULL)
    DEF("mcp-tool",            fn_mcp_tool,     4, 4);
    DEF("mcp-resource",        fn_mcp_resource, 3, 3);
    DEF("mcp-text",            fn_mcp_text,     1, 1);
    DEF("mcp-json",            fn_mcp_json,     1, 1);
    DEF("mcp-notify-progress", fn_mcp_progress, 3, 3);
    DEF("mcp-serve",           fn_mcp_serve,    0, 2);
#undef DEF
}
