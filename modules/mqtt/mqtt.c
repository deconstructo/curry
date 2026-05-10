/*
 * curry_mqtt — MQTT client module for Curry Scheme.
 *
 * Uses the Eclipse Paho C synchronous client (MQTTClient).
 * Links against libpaho-mqtt3cs (TLS) or libpaho-mqtt3c (plain TCP).
 * HAVE_MQTT_TLS is set by CMake when the SSL-capable library is present.
 *
 * Threading model: Paho delivers messages on its own callback thread.
 * Incoming messages and disconnect events are queued into a ring buffer
 * (mutex + condvar) so mqtt-receive can block on the Scheme thread without
 * re-entering the evaluator from a foreign thread.
 *
 * Scheme API:
 *   ; Plain TCP — positional arguments
 *   (mqtt-connect     host port client-id)                    -> client
 *   (mqtt-connect     host port client-id username password)  -> client
 *
 *   ; TLS — positional arguments (requires libpaho-mqtt3cs)
 *   (mqtt-connect-tls host port client-id)                    -> client
 *   (mqtt-connect-tls host port client-id ca-cert)            -> client
 *   (mqtt-connect-tls host port client-id ca-cert user pass)  -> client
 *
 *   ; Full-featured connect — options alist
 *   (mqtt-connect* host port client-id opts)                  -> client
 *   ; opts keys (all optional):
 *   ;   username        string
 *   ;   password        string
 *   ;   keep-alive      integer  seconds (default 60)
 *   ;   clean-session   bool     (default #t)
 *   ;   will-topic      string   Last Will topic
 *   ;   will-payload    string   Last Will payload
 *   ;   will-qos        integer  0/1/2 (default 0)
 *   ;   will-retain     bool     (default #f)
 *   ;   ca-cert         string   CA certificate path → enables TLS
 *   ;   verify-server   bool     TLS server cert check (default #t)
 *
 *   (mqtt-disconnect  client)                                 -> void
 *   (mqtt-connected?  client)                                 -> bool
 *   (mqtt-dropped     client)                                 -> integer
 *
 *   (mqtt-publish     client topic payload)                   -> void  QoS 0
 *   (mqtt-publish     client topic payload qos)               -> void
 *   (mqtt-publish     client topic payload qos retain?)       -> void
 *
 *   (mqtt-subscribe   client topic)                           -> void  QoS 1
 *   (mqtt-subscribe   client topic qos)                       -> void
 *   (mqtt-unsubscribe client topic)                           -> void
 *
 *   (mqtt-receive     client)                                 -> msg | #f
 *   (mqtt-receive     client timeout-ms)                      -> msg | #f
 *   ; msg is one of:
 *   ;   (topic . payload)       — normal message
 *   ;   (disconnect . "cause")  — connection lost event
 */

#include <curry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#include <MQTTClient.h>

/* ---- Incoming message queue ---- */

#define MQTT_QCAP 256

typedef struct {
    char  *topic;    /* NULL = disconnect sentinel */
    char  *payload;
    size_t paylen;
} MQTTQEntry;

typedef struct {
    MQTTClient       client;
    MQTTQEntry       q[MQTT_QCAP];
    int              qhead;
    int              qtail;
    pthread_mutex_t  qmtx;
    pthread_cond_t   qcv;
    int              dropped;        /* messages lost due to full queue */
#ifdef HAVE_MQTT_TLS
    char            *ca_cert;        /* heap copy of CA cert path (may be NULL) */
    MQTTClient_SSLOptions ssl_opts;  /* embedded — avoids dangling pointer */
#endif
} MQTTConn;

/* ---- Paho callbacks ---- */

static int msg_arrived(void *ctx, char *topic, int topicLen,
                       MQTTClient_message *msg) {
    (void)topicLen;
    MQTTConn *c = (MQTTConn *)ctx;

    pthread_mutex_lock(&c->qmtx);
    int next = (c->qtail + 1) % MQTT_QCAP;
    if (next == c->qhead) {
        /* Queue full — count the drop and discard */
        c->dropped++;
    } else {
        MQTTQEntry *e = &c->q[c->qtail];
        e->topic   = strdup(topic);
        e->paylen  = (size_t)msg->payloadlen;
        e->payload = malloc(e->paylen + 1);
        if (e->topic && e->payload) {
            memcpy(e->payload, msg->payload, e->paylen);
            ((char *)e->payload)[e->paylen] = '\0';
            c->qtail = next;
            pthread_cond_signal(&c->qcv);
        } else {
            free(e->topic);
            free(e->payload);
            e->topic = e->payload = NULL;
            c->dropped++;
        }
    }
    pthread_mutex_unlock(&c->qmtx);

    MQTTClient_freeMessage(&msg);
    MQTTClient_free(topic);
    return 1;
}

/*
 * Queue a disconnect sentinel: topic=NULL, payload=cause string.
 * Called from Paho's internal thread — must not touch Scheme heap.
 */
static void conn_lost(void *ctx, char *cause) {
    MQTTConn *c = (MQTTConn *)ctx;
    pthread_mutex_lock(&c->qmtx);
    int next = (c->qtail + 1) % MQTT_QCAP;
    if (next != c->qhead) {
        MQTTQEntry *e = &c->q[c->qtail];
        e->topic   = NULL;   /* sentinel */
        e->payload = strdup(cause ? cause : "connection lost");
        e->paylen  = e->payload ? strlen(e->payload) : 0;
        c->qtail   = next;
        pthread_cond_signal(&c->qcv);
    }
    pthread_mutex_unlock(&c->qmtx);
}

/* ---- Connection handle encoding ---- */

static curry_val conn_tag(void) { return curry_make_symbol("mqtt-conn"); }

static curry_val conn_to_val(MQTTConn *c) {
    curry_val bv = curry_make_bytevector(sizeof(MQTTConn *), 0);
    for (size_t i = 0; i < sizeof(MQTTConn *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&c)[i]);
    return curry_make_pair(conn_tag(), bv);
}

static MQTTConn *val_to_conn(curry_val v) {
    if (!curry_is_pair(v) || curry_car(v) != conn_tag())
        curry_error("mqtt: not an mqtt client handle");
    curry_val bv = curry_cdr(v);
    MQTTConn *c;
    for (size_t i = 0; i < sizeof(MQTTConn *); i++)
        ((uint8_t *)&c)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return c;
}

/* ---- Rich connect options ---- */

typedef struct {
    const char *username;
    const char *password;
    int         keep_alive;     /* seconds; 0 → default 60 */
    int         clean_session;  /* 1 = clean (default), 0 = persistent */
    /* Last Will & Testament */
    const char *will_topic;
    const char *will_payload;
    int         will_qos;
    int         will_retain;
#ifdef HAVE_MQTT_TLS
    const char *ca_cert;        /* NULL = no TLS */
    int         verify_server;  /* 1 = verify (default) */
#endif
} ConnOpts;

static ConnOpts default_opts(void) {
    ConnOpts o;
    memset(&o, 0, sizeof(o));
    o.keep_alive    = 60;
    o.clean_session = 1;
    o.will_qos      = 0;
    o.will_retain   = 0;
#ifdef HAVE_MQTT_TLS
    o.verify_server = 1;
#endif
    return o;
}

/* Parse an options alist into ConnOpts. Unknown keys are silently ignored. */
static ConnOpts parse_opts_alist(curry_val alist) {
    ConnOpts o = default_opts();
    for (curry_val p = alist; curry_is_pair(p); p = curry_cdr(p)) {
        curry_val kv = curry_car(p);
        if (!curry_is_pair(kv)) continue;
        curry_val k = curry_car(kv);
        curry_val v = curry_cdr(kv);
        if (!curry_is_symbol(k)) continue;
        const char *key = curry_symbol(k);
        if      (!strcmp(key, "username")      && curry_is_string(v))  o.username      = curry_string(v);
        else if (!strcmp(key, "password")      && curry_is_string(v))  o.password      = curry_string(v);
        else if (!strcmp(key, "keep-alive")    && curry_is_fixnum(v))  o.keep_alive    = (int)curry_fixnum(v);
        else if (!strcmp(key, "clean-session"))                         o.clean_session = curry_is_true(v) ? 1 : 0;
        else if (!strcmp(key, "will-topic")    && curry_is_string(v))  o.will_topic    = curry_string(v);
        else if (!strcmp(key, "will-payload")  && curry_is_string(v))  o.will_payload  = curry_string(v);
        else if (!strcmp(key, "will-qos")      && curry_is_fixnum(v))  o.will_qos      = (int)curry_fixnum(v);
        else if (!strcmp(key, "will-retain"))                           o.will_retain   = curry_is_true(v) ? 1 : 0;
#ifdef HAVE_MQTT_TLS
        else if (!strcmp(key, "ca-cert")       && curry_is_string(v))  o.ca_cert       = curry_string(v);
        else if (!strcmp(key, "verify-server"))                         o.verify_server = curry_is_true(v) ? 1 : 0;
#endif
    }
    return o;
}

/* ---- Core connection logic ---- */

static MQTTConn *do_connect(const char *host, int port,
                             const char *client_id, const ConnOpts *o) {
    char uri[512];
#ifdef HAVE_MQTT_TLS
    int use_tls = (o->ca_cert != NULL);
    snprintf(uri, sizeof(uri), use_tls ? "ssl://%s:%d" : "tcp://%s:%d", host, port);
#else
    snprintf(uri, sizeof(uri), "tcp://%s:%d", host, port);
#endif

    MQTTConn *c = calloc(1, sizeof(MQTTConn));
    if (!c) curry_error("mqtt-connect: out of memory");

    pthread_mutex_init(&c->qmtx, NULL);
    pthread_cond_init(&c->qcv,  NULL);

#ifdef HAVE_MQTT_TLS
    if (o->ca_cert) {
        c->ca_cert = strdup(o->ca_cert);  /* keep alive for session */
        c->ssl_opts = (MQTTClient_SSLOptions)MQTTClient_SSLOptions_initializer;
        c->ssl_opts.enableServerCertAuth = o->verify_server;
        c->ssl_opts.trustStore           = c->ca_cert;
    }
#endif

    int rc = MQTTClient_create(&c->client, uri, client_id,
                               MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        pthread_mutex_destroy(&c->qmtx);
        pthread_cond_destroy(&c->qcv);
#ifdef HAVE_MQTT_TLS
        free(c->ca_cert);
#endif
        free(c);
        curry_error("mqtt-connect: create failed (rc=%d)", rc);
    }

    MQTTClient_setCallbacks(c->client, c, conn_lost, msg_arrived, NULL);

    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    opts.keepAliveInterval = o->keep_alive > 0 ? o->keep_alive : 60;
    opts.cleansession      = o->clean_session;
    if (o->username) {
        opts.username = o->username;
        opts.password = o->password;
    }

    /* Last Will & Testament */
    MQTTClient_willOptions will = MQTTClient_willOptions_initializer;
    if (o->will_topic) {
        will.topicName = o->will_topic;
        will.message   = o->will_payload ? o->will_payload : "";
        will.qos       = o->will_qos;
        will.retained  = o->will_retain;
        opts.will      = &will;
    }

#ifdef HAVE_MQTT_TLS
    if (use_tls) opts.ssl = &c->ssl_opts;
#endif

    rc = MQTTClient_connect(c->client, &opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        MQTTClient_destroy(&c->client);
        pthread_mutex_destroy(&c->qmtx);
        pthread_cond_destroy(&c->qcv);
#ifdef HAVE_MQTT_TLS
        free(c->ca_cert);
#endif
        free(c);
        curry_error("mqtt-connect: connection to %s failed (rc=%d)", uri, rc);
    }

    return c;
}

/* ---- Scheme procedures ---- */

/* (mqtt-connect host port client-id [username password]) */
static curry_val fn_connect(int ac, curry_val *av, void *ud) {
    (void)ud;
    ConnOpts o = default_opts();
    if (ac >= 5) {
        o.username = curry_string(av[3]);
        o.password = curry_string(av[4]);
    }
    return conn_to_val(do_connect(curry_string(av[0]),
                                  (int)curry_fixnum(av[1]),
                                  curry_string(av[2]), &o));
}

#ifdef HAVE_MQTT_TLS
/* (mqtt-connect-tls host port client-id [ca-cert [username password]]) */
static curry_val fn_connect_tls(int ac, curry_val *av, void *ud) {
    (void)ud;
    ConnOpts o = default_opts();
    if (ac >= 4 && curry_is_string(av[3])) o.ca_cert  = curry_string(av[3]);
    /* even without a ca-cert, flag as TLS by using a non-NULL sentinel */
    if (!o.ca_cert) o.ca_cert = "";    /* empty = system CA, still ssl:// */
    if (ac >= 6) {
        o.username = curry_string(av[4]);
        o.password = curry_string(av[5]);
    }
    return conn_to_val(do_connect(curry_string(av[0]),
                                  (int)curry_fixnum(av[1]),
                                  curry_string(av[2]), &o));
}
#endif

/* (mqtt-connect* host port client-id opts-alist) */
static curry_val fn_connect_opts(int ac, curry_val *av, void *ud) {
    (void)ud;
    ConnOpts o = (ac >= 4) ? parse_opts_alist(av[3]) : default_opts();
    return conn_to_val(do_connect(curry_string(av[0]),
                                  (int)curry_fixnum(av[1]),
                                  curry_string(av[2]), &o));
}

/* (mqtt-disconnect client) */
static curry_val fn_disconnect(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    MQTTConn *c = val_to_conn(av[0]);
    MQTTClient_disconnect(c->client, 1000);
    MQTTClient_destroy(&c->client);

    pthread_mutex_lock(&c->qmtx);
    while (c->qhead != c->qtail) {
        free(c->q[c->qhead].topic);
        free(c->q[c->qhead].payload);
        c->qhead = (c->qhead + 1) % MQTT_QCAP;
    }
    pthread_mutex_unlock(&c->qmtx);

    pthread_mutex_destroy(&c->qmtx);
    pthread_cond_destroy(&c->qcv);
#ifdef HAVE_MQTT_TLS
    free(c->ca_cert);
#endif
    free(c);
    return curry_void();
}

/* (mqtt-connected? client) */
static curry_val fn_connected(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    return curry_make_bool(MQTTClient_isConnected(val_to_conn(av[0])->client) != 0);
}

/* (mqtt-dropped client) — count of messages lost due to queue overflow */
static curry_val fn_dropped(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    MQTTConn *c = val_to_conn(av[0]);
    pthread_mutex_lock(&c->qmtx);
    int d = c->dropped;
    pthread_mutex_unlock(&c->qmtx);
    return curry_make_fixnum(d);
}

/* (mqtt-publish client topic payload [qos [retain?]]) */
static curry_val fn_publish(int ac, curry_val *av, void *ud) {
    (void)ud;
    MQTTConn   *c       = val_to_conn(av[0]);
    const char *topic   = curry_string(av[1]);
    const char *payload = curry_string(av[2]);
    int         qos     = (ac >= 4) ? (int)curry_fixnum(av[3]) : 0;
    int         retain  = (ac >= 5) ? (curry_is_true(av[4]) ? 1 : 0) : 0;
    size_t      paylen  = strlen(payload);

    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publish(c->client, topic, (int)paylen,
                                (void *)payload, qos, retain, &token);
    if (rc != MQTTCLIENT_SUCCESS)
        curry_error("mqtt-publish: failed (rc=%d)", rc);
    if (qos > 0)
        MQTTClient_waitForCompletion(c->client, token, 5000UL);
    return curry_void();
}

/* (mqtt-subscribe client topic [qos]) */
static curry_val fn_subscribe(int ac, curry_val *av, void *ud) {
    (void)ud;
    MQTTConn   *c     = val_to_conn(av[0]);
    const char *topic = curry_string(av[1]);
    int         qos   = (ac >= 3) ? (int)curry_fixnum(av[2]) : 1;
    int rc = MQTTClient_subscribe(c->client, topic, qos);
    if (rc != MQTTCLIENT_SUCCESS)
        curry_error("mqtt-subscribe: failed (rc=%d)", rc);
    return curry_void();
}

/* (mqtt-unsubscribe client topic) */
static curry_val fn_unsubscribe(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    MQTTClient_unsubscribe(val_to_conn(av[0])->client, curry_string(av[1]));
    return curry_void();
}

/*
 * (mqtt-receive client [timeout-ms])
 *
 * Blocks until a message arrives or the timeout expires.
 * Returns one of:
 *   (topic . payload)       — normal message
 *   (disconnect . "cause")  — connection lost event
 *   #f                      — timeout
 */
static curry_val fn_receive(int ac, curry_val *av, void *ud) {
    (void)ud;
    MQTTConn *c          = val_to_conn(av[0]);
    int       timeout_ms = (ac >= 2) ? (int)curry_fixnum(av[1]) : 5000;

    pthread_mutex_lock(&c->qmtx);

    if (c->qhead == c->qtail && timeout_ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  +=  timeout_ms / 1000;
        ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&c->qcv, &c->qmtx, &ts);
    }

    if (c->qhead == c->qtail) {
        pthread_mutex_unlock(&c->qmtx);
        return curry_make_bool(false);
    }

    MQTTQEntry e = c->q[c->qhead];
    c->qhead = (c->qhead + 1) % MQTT_QCAP;
    pthread_mutex_unlock(&c->qmtx);

    /* Disconnect sentinel: topic == NULL */
    if (e.topic == NULL) {
        curry_val cause = curry_make_string(e.payload ? e.payload : "");
        free(e.payload);
        return curry_make_pair(curry_make_symbol("disconnect"), cause);
    }

    curry_val topic   = curry_make_string(e.topic);
    curry_val payload = curry_make_string(e.payload);
    free(e.topic);
    free(e.payload);
    return curry_make_pair(topic, payload);
}

/* ---- Module entry point ---- */

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "mqtt-connect",      fn_connect,      3, 5, NULL);
    curry_define_fn(vm, "mqtt-connect*",     fn_connect_opts, 3, 4, NULL);
    curry_define_fn(vm, "mqtt-disconnect",   fn_disconnect,   1, 1, NULL);
    curry_define_fn(vm, "mqtt-connected?",   fn_connected,    1, 1, NULL);
    curry_define_fn(vm, "mqtt-dropped",      fn_dropped,      1, 1, NULL);
    curry_define_fn(vm, "mqtt-publish",      fn_publish,      3, 5, NULL);
    curry_define_fn(vm, "mqtt-subscribe",    fn_subscribe,    2, 3, NULL);
    curry_define_fn(vm, "mqtt-unsubscribe",  fn_unsubscribe,  2, 2, NULL);
    curry_define_fn(vm, "mqtt-receive",      fn_receive,      1, 2, NULL);
#ifdef HAVE_MQTT_TLS
    curry_define_fn(vm, "mqtt-connect-tls",  fn_connect_tls,  3, 6, NULL);
#endif
}
