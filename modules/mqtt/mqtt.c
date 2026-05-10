/*
 * curry_mqtt — MQTT client module for Curry Scheme.
 *
 * Uses the Eclipse Paho C synchronous client (MQTTClient).
 * Links against libpaho-mqtt3cs when built with TLS support,
 * libpaho-mqtt3c otherwise.  HAVE_MQTT_TLS is defined by CMake
 * when the SSL-capable library is present.
 *
 * Incoming messages arrive on a Paho callback thread; they are
 * queued into a native ring buffer (mutex + condvar) so that
 * mqtt-receive can block safely on the Scheme thread without
 * calling any Paho functions from within the callback.
 *
 * Scheme API:
 *   ; Plain TCP
 *   (mqtt-connect     host port client-id)                   -> client
 *   (mqtt-connect     host port client-id username password) -> client
 *
 *   ; TLS (requires libpaho-mqtt3cs at build time)
 *   (mqtt-connect-tls host port client-id)                   -> client
 *   (mqtt-connect-tls host port client-id ca-cert)           -> client
 *   (mqtt-connect-tls host port client-id ca-cert user pass) -> client
 *
 *   (mqtt-disconnect  client)                                -> void
 *   (mqtt-connected?  client)                                -> bool
 *
 *   (mqtt-publish     client topic payload)                  -> void  (QoS 0)
 *   (mqtt-publish     client topic payload qos)              -> void  (QoS 0/1/2)
 *   (mqtt-publish     client topic payload qos retain?)      -> void
 *
 *   (mqtt-subscribe   client topic)                          -> void  (QoS 1)
 *   (mqtt-subscribe   client topic qos)                      -> void
 *   (mqtt-unsubscribe client topic)                          -> void
 *
 *   (mqtt-receive     client)                                -> (topic . payload) | #f
 *   (mqtt-receive     client timeout-ms)                     -> ...   (5 s default)
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
    char  *topic;
    char  *payload;
    size_t paylen;
} MQTTQEntry;

typedef struct {
    MQTTClient      client;
    MQTTQEntry      q[MQTT_QCAP];
    int             qhead;
    int             qtail;
    pthread_mutex_t qmtx;
    pthread_cond_t  qcv;
} MQTTConn;

/* ---- Paho callbacks ---- */

/* Called by Paho's internal thread when a subscribed message arrives. */
static int msg_arrived(void *ctx, char *topic, int topicLen,
                       MQTTClient_message *msg) {
    (void)topicLen;
    MQTTConn *c = (MQTTConn *)ctx;

    pthread_mutex_lock(&c->qmtx);
    int next = (c->qtail + 1) % MQTT_QCAP;
    if (next != c->qhead) {   /* drop silently when queue is full */
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
        }
    }
    pthread_mutex_unlock(&c->qmtx);

    /* Free Paho's copies — safe to call from the callback */
    MQTTClient_freeMessage(&msg);
    MQTTClient_free(topic);
    return 1;
}

static void conn_lost(void *ctx, char *cause) {
    (void)ctx; (void)cause;
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

/* ---- Connection helper ---- */

typedef struct {
    const char             *username;
    const char             *password;
#ifdef HAVE_MQTT_TLS
    MQTTClient_SSLOptions  *ssl;
#endif
} ConnExtra;

static MQTTConn *do_connect(const char *host, int port,
                             const char *client_id, ConnExtra *ex) {
    char uri[512];
#ifdef HAVE_MQTT_TLS
    if (ex->ssl)
        snprintf(uri, sizeof(uri), "ssl://%s:%d", host, port);
    else
#endif
        snprintf(uri, sizeof(uri), "tcp://%s:%d", host, port);

    MQTTConn *c = calloc(1, sizeof(MQTTConn));
    if (!c) curry_error("mqtt-connect: out of memory");

    pthread_mutex_init(&c->qmtx, NULL);
    pthread_cond_init(&c->qcv,  NULL);

    int rc = MQTTClient_create(&c->client, uri, client_id,
                               MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        pthread_mutex_destroy(&c->qmtx);
        pthread_cond_destroy(&c->qcv);
        free(c);
        curry_error("mqtt-connect: create failed (rc=%d)", rc);
    }

    MQTTClient_setCallbacks(c->client, c, conn_lost, msg_arrived, NULL);

    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    opts.keepAliveInterval = 60;
    opts.cleansession      = 1;
    if (ex->username) {
        opts.username = ex->username;
        opts.password = ex->password;
    }
#ifdef HAVE_MQTT_TLS
    if (ex->ssl) opts.ssl = ex->ssl;
#endif

    rc = MQTTClient_connect(c->client, &opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        MQTTClient_destroy(&c->client);
        pthread_mutex_destroy(&c->qmtx);
        pthread_cond_destroy(&c->qcv);
        free(c);
        curry_error("mqtt-connect: connection to %s failed (rc=%d)", uri, rc);
    }

    return c;
}

/* ---- Scheme procedures ---- */

/* (mqtt-connect host port client-id [username password]) */
static curry_val fn_connect(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *host = curry_string(av[0]);
    int         port = (int)curry_fixnum(av[1]);
    const char *cid  = curry_string(av[2]);
    ConnExtra ex = {0};
    if (ac >= 5) {
        ex.username = curry_string(av[3]);
        ex.password = curry_string(av[4]);
    }
    return conn_to_val(do_connect(host, port, cid, &ex));
}

#ifdef HAVE_MQTT_TLS
/* (mqtt-connect-tls host port client-id [ca-cert [username password]]) */
static curry_val fn_connect_tls(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *host = curry_string(av[0]);
    int         port = (int)curry_fixnum(av[1]);
    const char *cid  = curry_string(av[2]);

    MQTTClient_SSLOptions ssl = MQTTClient_SSLOptions_initializer;
    ssl.enableServerCertAuth = 1;
    if (ac >= 4 && curry_is_string(av[3]))
        ssl.trustStore = curry_string(av[3]);

    ConnExtra ex = {0};
    ex.ssl = &ssl;
    if (ac >= 6) {
        ex.username = curry_string(av[4]);
        ex.password = curry_string(av[5]);
    }
    return conn_to_val(do_connect(host, port, cid, &ex));
}
#endif

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
    free(c);
    return curry_void();
}

/* (mqtt-connected? client) */
static curry_val fn_connected(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    MQTTConn *c = val_to_conn(av[0]);
    return curry_make_bool(MQTTClient_isConnected(c->client) != 0);
}

/* (mqtt-publish client topic payload [qos [retain?]]) */
static curry_val fn_publish(int ac, curry_val *av, void *ud) {
    (void)ud;
    MQTTConn   *c       = val_to_conn(av[0]);
    const char *topic   = curry_string(av[1]);
    const char *payload = curry_string(av[2]);
    int         qos     = (ac >= 4) ? (int)curry_fixnum(av[3]) : 0;
    int         retain  = (ac >= 5) ? curry_is_true(av[4])     : 0;
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
    MQTTConn   *c     = val_to_conn(av[0]);
    const char *topic = curry_string(av[1]);
    MQTTClient_unsubscribe(c->client, topic);
    return curry_void();
}

/* (mqtt-receive client [timeout-ms])
   Returns (topic . payload) or #f on timeout. */
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
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&c->qcv, &c->qmtx, &ts);
    }

    if (c->qhead == c->qtail) {
        pthread_mutex_unlock(&c->qmtx);
        return curry_make_bool(false);
    }

    MQTTQEntry e = c->q[c->qhead];
    c->qhead = (c->qhead + 1) % MQTT_QCAP;
    pthread_mutex_unlock(&c->qmtx);

    curry_val topic   = curry_make_string(e.topic);
    curry_val payload = curry_make_string(e.payload);
    free(e.topic);
    free(e.payload);

    return curry_make_pair(topic, payload);
}

/* ---- Module entry point ---- */

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "mqtt-connect",     fn_connect,     3, 5, NULL);
    curry_define_fn(vm, "mqtt-disconnect",  fn_disconnect,  1, 1, NULL);
    curry_define_fn(vm, "mqtt-connected?",  fn_connected,   1, 1, NULL);
    curry_define_fn(vm, "mqtt-publish",     fn_publish,     3, 5, NULL);
    curry_define_fn(vm, "mqtt-subscribe",   fn_subscribe,   2, 3, NULL);
    curry_define_fn(vm, "mqtt-unsubscribe", fn_unsubscribe, 2, 2, NULL);
    curry_define_fn(vm, "mqtt-receive",     fn_receive,     1, 2, NULL);
#ifdef HAVE_MQTT_TLS
    curry_define_fn(vm, "mqtt-connect-tls", fn_connect_tls, 3, 6, NULL);
#endif
}
