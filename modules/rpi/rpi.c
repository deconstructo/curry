/* rpi.c — Raspberry Pi / Linux embedded hardware module for Curry Scheme.
 *
 * Provides GPIO (via libgpiod), I2C, SPI, PWM (sysfs), Camera (V4L2),
 * UART (termios), 1-Wire (sysfs), hardware watchdog, and board-info
 * interfaces.  Linux-only.  Not supported on macOS.
 *
 * Handles are tagged pairs:
 *   ("gpio"     . bv)  — gpiod_line*
 *   ("i2c"      . bv)  — int fd
 *   ("spi"      . bv)  — int fd
 *   ("pwm"      . bv)  — #(chip channel sysfs-path)
 *   ("watcher"  . bv)  — GpioWatcher* (interrupt watcher thread)
 *   ("camera"   . bv)  — Camera*
 *   ("uart"     . bv)  — int fd
 *   ("watchdog" . bv)  — int fd
 *
 * Dependencies:
 *   GPIO:    libgpiod  (sudo apt install libgpiod-dev)
 *   I2C:     kernel headers only  (linux/i2c-dev.h)
 *   SPI:     kernel headers only  (linux/spi/spidev.h)
 *   PWM:     sysfs — no extra library
 *   Camera:  V4L2 kernel headers  (linux/videodev2.h)
 *   UART:    POSIX termios — no extra library
 *   1-Wire:  sysfs — no extra library
 *   Watchdog: linux/watchdog.h — no extra library
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <curry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <poll.h>
#include <dirent.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>
#include <linux/videodev2.h>
#include <linux/watchdog.h>
#include <gpiod.h>
#define GC_THREADS
#include <gc/gc.h>

/* ---- pointer packing (same idiom as sync module) ---- */

static curry_val pack_ptr(void *ptr) {
    curry_val bv = curry_make_bytevector(sizeof(void *), 0);
    for (size_t i = 0; i < sizeof(void *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&ptr)[i]);
    return bv;
}
static void *unpack_ptr(curry_val bv) {
    void *ptr = NULL;
    for (size_t i = 0; i < sizeof(void *); i++)
        ((uint8_t *)&ptr)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return ptr;
}

static curry_val pack_fd(int fd) {
    /* pack a plain int as a pointer-sized value */
    intptr_t v = fd;
    return pack_ptr((void *)v);
}
static int unpack_fd(curry_val bv) {
    return (int)(intptr_t)unpack_ptr(bv);
}

static int has_tag(curry_val v, const char *tag) {
    return curry_is_pair(v) &&
           curry_is_symbol(curry_car(v)) &&
           strcmp(curry_symbol(curry_car(v)), tag) == 0;
}

/* ---- GPIO (libgpiod) ---- */

static curry_val wrap_gpio(struct gpiod_line *line) {
    return curry_make_pair(curry_make_symbol("gpio"), pack_ptr(line));
}
static struct gpiod_line *get_gpio(curry_val v, const char *ctx) {
    if (!has_tag(v, "gpio")) curry_error("%s: expected gpio handle", ctx);
    return (struct gpiod_line *)unpack_ptr(curry_cdr(v));
}

/* (gpio-open chip-num line-num direction) → gpio-handle
 * direction: 'input or 'output */
static curry_val fn_gpio_open(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_fixnum(av[0])) curry_error("gpio-open: chip must be fixnum");
    if (!curry_is_fixnum(av[1])) curry_error("gpio-open: line must be fixnum");
    if (!curry_is_symbol(av[2])) curry_error("gpio-open: direction must be 'input or 'output");

    int chip_num = (int)curry_fixnum(av[0]);
    int line_num = (int)curry_fixnum(av[1]);
    const char *dir = curry_symbol(av[2]);

    char chip_path[32];
    snprintf(chip_path, sizeof(chip_path), "/dev/gpiochip%d", chip_num);

    struct gpiod_chip *chip = gpiod_chip_open(chip_path);
    if (!chip)
        curry_error("gpio-open: cannot open %s: %s", chip_path, strerror(errno));

    struct gpiod_line *line = gpiod_chip_get_line(chip, (unsigned)line_num);
    if (!line) {
        gpiod_chip_close(chip);
        curry_error("gpio-open: cannot get line %d: %s", line_num, strerror(errno));
    }

    int rc;
    if (strcmp(dir, "input") == 0) {
        rc = gpiod_line_request_input(line, "curry");
    } else if (strcmp(dir, "output") == 0) {
        rc = gpiod_line_request_output(line, "curry", 0);
    } else if (strcmp(dir, "rising") == 0) {
        rc = gpiod_line_request_rising_edge_events(line, "curry");
    } else if (strcmp(dir, "falling") == 0) {
        rc = gpiod_line_request_falling_edge_events(line, "curry");
    } else if (strcmp(dir, "both") == 0) {
        rc = gpiod_line_request_both_edges_events(line, "curry");
    } else {
        gpiod_chip_close(chip);
        curry_error("gpio-open: direction must be 'input, 'output, 'rising, 'falling, or 'both, got '%s'", dir);
    }

    if (rc < 0) {
        gpiod_chip_close(chip);
        curry_error("gpio-open: line request failed: %s", strerror(errno));
    }

    /* chip handle is kept alive via the line — close only on gpio-close */
    return wrap_gpio(line);
}

/* (gpio-read handle) → 0 or 1 */
static curry_val fn_gpio_read(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    struct gpiod_line *line = get_gpio(av[0], "gpio-read");
    int val = gpiod_line_get_value(line);
    if (val < 0) curry_error("gpio-read: %s", strerror(errno));
    return curry_make_fixnum(val);
}

/* (gpio-write handle value) → void */
static curry_val fn_gpio_write(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    struct gpiod_line *line = get_gpio(av[0], "gpio-write");
    if (!curry_is_fixnum(av[1])) curry_error("gpio-write: value must be 0 or 1");
    int val = (int)curry_fixnum(av[1]);
    if (gpiod_line_set_value(line, val) < 0)
        curry_error("gpio-write: %s", strerror(errno));
    return curry_void();
}

/* (gpio-close handle) → void */
static curry_val fn_gpio_close(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    struct gpiod_line *line = get_gpio(av[0], "gpio-close");
    struct gpiod_chip *chip = gpiod_line_get_chip(line);
    gpiod_line_release(line);
    gpiod_chip_close(chip);
    return curry_void();
}

/* (gpio? v) → bool */
static curry_val fn_gpio_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(has_tag(av[0], "gpio"));
}

/* (gpio-wait-edge handle timeout-ms) → 'rising | 'falling | #f
 * timeout-ms: milliseconds; -1 = wait forever.
 * The line must have been opened with direction 'rising, 'falling, or 'both. */
static curry_val fn_gpio_wait_edge(int ac, curry_val *av, void *ud) {
    (void)ud;
    struct gpiod_line *line = get_gpio(av[0], "gpio-wait-edge");
    int timeout_ms = (ac >= 2 && curry_is_fixnum(av[1])) ? (int)curry_fixnum(av[1]) : -1;

    int fd = gpiod_line_event_get_fd(line);
    if (fd < 0)
        curry_error("gpio-wait-edge: line not configured for edge events — open with 'rising, 'falling, or 'both");

    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int r = poll(&pfd, 1, timeout_ms);
    if (r < 0)  curry_error("gpio-wait-edge: poll: %s", strerror(errno));
    if (r == 0) return curry_make_bool(false);   /* timeout */

    struct gpiod_line_event ev;
    if (gpiod_line_event_read(line, &ev) < 0)
        curry_error("gpio-wait-edge: read failed: %s", strerror(errno));

    return (ev.event_type == GPIOD_LINE_EVENT_RISING_EDGE)
        ? curry_make_symbol("rising") : curry_make_symbol("falling");
}

/* ---- GPIO watcher (interrupt-driven background thread) ---- */

typedef struct {
    struct gpiod_line *line;
    curry_val          callback; /* Scheme proc — kept alive via GC_add_roots below */
    int                pipe_rd;
    int                pipe_wr;
    pthread_t          thread;
    volatile int       active;
} GpioWatcher;

static curry_val wrap_watcher(GpioWatcher *w) {
    return curry_make_pair(curry_make_symbol("watcher"), pack_ptr(w));
}
static GpioWatcher *get_watcher(curry_val v, const char *ctx) {
    if (!has_tag(v, "watcher")) curry_error("%s: expected watcher handle", ctx);
    return (GpioWatcher *)unpack_ptr(curry_cdr(v));
}

static void *watcher_thread_fn(void *arg) {
    GpioWatcher *w = (GpioWatcher *)arg;

    struct GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);

    int gpio_fd = gpiod_line_event_get_fd(w->line);
    struct pollfd fds[2] = {
        { .fd = gpio_fd,    .events = POLLIN },
        { .fd = w->pipe_rd, .events = POLLIN },
    };

    while (w->active) {
        int r = poll(fds, 2, -1);
        if (r < 0 || (fds[1].revents & POLLIN)) break;
        if (!(fds[0].revents & POLLIN)) continue;

        struct gpiod_line_event ev;
        if (gpiod_line_event_read(w->line, &ev) < 0) continue;

        curry_val edge = (ev.event_type == GPIOD_LINE_EVENT_RISING_EDGE)
            ? curry_make_symbol("rising") : curry_make_symbol("falling");
        int64_t ts_ns = (int64_t)ev.ts.tv_sec * 1000000000LL + ev.ts.tv_nsec;
        curry_val args[2] = { edge, curry_make_fixnum(ts_ns) };
        /* NOTE: do not call gpio-unwatch from within the callback — deadlock. */
        curry_apply(w->callback, 2, args);
    }

    GC_unregister_my_thread();
    return NULL;
}

/* (gpio-watch handle proc) → watcher-handle
 * proc is called as (proc edge timestamp-ns) on each interrupt.
 * edge: 'rising or 'falling; timestamp-ns: nanoseconds since epoch.
 * The line must have been opened with direction 'rising, 'falling, or 'both. */
static curry_val fn_gpio_watch(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    struct gpiod_line *line = get_gpio(av[0], "gpio-watch");
    if (!curry_is_procedure(av[1])) curry_error("gpio-watch: second argument must be a procedure");

    int fd = gpiod_line_event_get_fd(line);
    if (fd < 0)
        curry_error("gpio-watch: line not configured for edge events — open with 'rising, 'falling, or 'both");

    int pipefds[2];
    if (pipe(pipefds) < 0)
        curry_error("gpio-watch: pipe: %s", strerror(errno));

    GpioWatcher *w = malloc(sizeof(GpioWatcher));
    if (!w) { close(pipefds[0]); close(pipefds[1]); curry_error("gpio-watch: out of memory"); }

    w->line     = line;
    w->callback = av[1];
    w->pipe_rd  = pipefds[0];
    w->pipe_wr  = pipefds[1];
    w->active   = 1;

    /* Keep w->callback alive: GC scans the memory range [w, w+sizeof(*w)). */
    GC_add_roots(w, (char *)w + sizeof(*w));

    if (pthread_create(&w->thread, NULL, watcher_thread_fn, w) != 0) {
        GC_remove_roots(w, (char *)w + sizeof(*w));
        close(pipefds[0]); close(pipefds[1]); free(w);
        curry_error("gpio-watch: pthread_create: %s", strerror(errno));
    }

    return wrap_watcher(w);
}

/* (gpio-unwatch watcher-handle) → void
 * Signals the watcher thread to stop and waits for it to exit.
 * Do not call from within the watcher callback — it will deadlock. */
static curry_val fn_gpio_unwatch(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    GpioWatcher *w = get_watcher(av[0], "gpio-unwatch");
    w->active = 0;
    (void)write(w->pipe_wr, "x", 1);   /* wake the poll */
    pthread_join(w->thread, NULL);
    GC_remove_roots(w, (char *)w + sizeof(*w));
    close(w->pipe_rd);
    close(w->pipe_wr);
    free(w);
    return curry_void();
}

/* (watcher? v) → bool */
static curry_val fn_watcher_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(has_tag(av[0], "watcher"));
}

/* ---- I2C ---- */

static curry_val wrap_i2c(int fd) {
    return curry_make_pair(curry_make_symbol("i2c"), pack_fd(fd));
}
static int get_i2c(curry_val v, const char *ctx) {
    if (!has_tag(v, "i2c")) curry_error("%s: expected i2c handle", ctx);
    return unpack_fd(curry_cdr(v));
}

/* (i2c-open bus-num) → i2c-handle  — opens /dev/i2c-N */
static curry_val fn_i2c_open(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_fixnum(av[0])) curry_error("i2c-open: bus must be fixnum");
    int bus = (int)curry_fixnum(av[0]);
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    int fd = open(path, O_RDWR);
    if (fd < 0) curry_error("i2c-open: cannot open %s: %s", path, strerror(errno));
    return wrap_i2c(fd);
}

/* (i2c-read handle addr reg nbytes) → bytevector */
static curry_val fn_i2c_read(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int fd   = get_i2c(av[0], "i2c-read");
    int addr = (int)curry_fixnum(av[1]);
    int reg  = (int)curry_fixnum(av[2]);
    int n    = (int)curry_fixnum(av[3]);

    if (ioctl(fd, I2C_SLAVE, addr) < 0)
        curry_error("i2c-read: cannot select addr 0x%02x: %s", addr, strerror(errno));

    uint8_t reg_byte = (uint8_t)reg;
    if (write(fd, &reg_byte, 1) != 1)
        curry_error("i2c-read: register write failed: %s", strerror(errno));

    curry_val bv = curry_make_bytevector(n, 0);
    uint8_t buf[256];
    if (n > 256) curry_error("i2c-read: nbytes too large (max 256)");
    if (read(fd, buf, (size_t)n) != n)
        curry_error("i2c-read: read failed: %s", strerror(errno));
    for (int i = 0; i < n; i++)
        curry_bytevector_set(bv, (uint32_t)i, buf[i]);
    return bv;
}

/* (i2c-write handle addr reg bytevector) → void */
static curry_val fn_i2c_write(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int fd   = get_i2c(av[0], "i2c-write");
    int addr = (int)curry_fixnum(av[1]);
    int reg  = (int)curry_fixnum(av[2]);

    if (!curry_is_bytevector(av[3])) curry_error("i2c-write: data must be bytevector");
    int dlen = (int)curry_bytevector_length(av[3]);

    if (ioctl(fd, I2C_SLAVE, addr) < 0)
        curry_error("i2c-write: cannot select addr 0x%02x: %s", addr, strerror(errno));

    uint8_t buf[257];
    buf[0] = (uint8_t)reg;
    for (int i = 0; i < dlen && i < 256; i++)
        buf[i + 1] = curry_bytevector_ref(av[3], (uint32_t)i);

    if (write(fd, buf, (size_t)(dlen + 1)) != dlen + 1)
        curry_error("i2c-write: write failed: %s", strerror(errno));

    return curry_void();
}

/* (i2c-close handle) → void */
static curry_val fn_i2c_close(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    close(get_i2c(av[0], "i2c-close"));
    return curry_void();
}

/* (i2c? v) → bool */
static curry_val fn_i2c_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(has_tag(av[0], "i2c"));
}

/* ---- SPI ---- */

static curry_val wrap_spi(int fd) {
    return curry_make_pair(curry_make_symbol("spi"), pack_fd(fd));
}
static int get_spi(curry_val v, const char *ctx) {
    if (!has_tag(v, "spi")) curry_error("%s: expected spi handle", ctx);
    return unpack_fd(curry_cdr(v));
}

/* (spi-open bus device speed-hz mode) → spi-handle
 * mode: 0-3 (SPI_MODE_x) */
static curry_val fn_spi_open(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_fixnum(av[0])) curry_error("spi-open: bus must be fixnum");
    if (!curry_is_fixnum(av[1])) curry_error("spi-open: device must be fixnum");

    int bus    = (int)curry_fixnum(av[0]);
    int device = (int)curry_fixnum(av[1]);
    uint32_t speed = curry_is_fixnum(av[2]) ? (uint32_t)curry_fixnum(av[2]) : 1000000;
    uint8_t  mode  = curry_is_fixnum(av[3]) ? (uint8_t)curry_fixnum(av[3])  : 0;

    char path[32];
    snprintf(path, sizeof(path), "/dev/spidev%d.%d", bus, device);
    int fd = open(path, O_RDWR);
    if (fd < 0) curry_error("spi-open: cannot open %s: %s", path, strerror(errno));

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
        close(fd);
        curry_error("spi-open: cannot set mode: %s", strerror(errno));
    }
    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        close(fd);
        curry_error("spi-open: cannot set speed: %s", strerror(errno));
    }
    uint8_t bits = 8;
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        close(fd);
        curry_error("spi-open: cannot set bits-per-word: %s", strerror(errno));
    }

    return wrap_spi(fd);
}

/* (spi-transfer handle tx-bytevector) → rx-bytevector (full-duplex) */
static curry_val fn_spi_transfer(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int fd = get_spi(av[0], "spi-transfer");
    if (!curry_is_bytevector(av[1])) curry_error("spi-transfer: tx must be bytevector");

    int len = (int)curry_bytevector_length(av[1]);
    uint8_t tx[4096], rx[4096];
    if (len > 4096) curry_error("spi-transfer: bytevector too large (max 4096)");

    for (int i = 0; i < len; i++)
        tx[i] = curry_bytevector_ref(av[1], (uint32_t)i);
    memset(rx, 0, (size_t)len);

    struct spi_ioc_transfer xfer = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = (uint32_t)len,
        .speed_hz      = 0,   /* use device default */
        .delay_usecs   = 0,
        .bits_per_word = 8,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &xfer) < 0)
        curry_error("spi-transfer: ioctl failed: %s", strerror(errno));

    curry_val bv = curry_make_bytevector(len, 0);
    for (int i = 0; i < len; i++)
        curry_bytevector_set(bv, (uint32_t)i, rx[i]);
    return bv;
}

/* (spi-close handle) → void */
static curry_val fn_spi_close(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    close(get_spi(av[0], "spi-close"));
    return curry_void();
}

/* (spi? v) → bool */
static curry_val fn_spi_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(has_tag(av[0], "spi"));
}

/* ---- PWM (sysfs /sys/class/pwm) ---- */

typedef struct { int chip; int channel; char base[64]; } PwmHandle;

static curry_val wrap_pwm(PwmHandle *h) {
    return curry_make_pair(curry_make_symbol("pwm"), pack_ptr(h));
}
static PwmHandle *get_pwm(curry_val v, const char *ctx) {
    if (!has_tag(v, "pwm")) curry_error("%s: expected pwm handle", ctx);
    return (PwmHandle *)unpack_ptr(curry_cdr(v));
}

static int pwm_write(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t n = write(fd, val, strlen(val));
    close(fd);
    return (n < 0) ? -1 : 0;
}

/* (pwm-open chip-num channel-num) → pwm-handle */
static curry_val fn_pwm_open(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_fixnum(av[0])) curry_error("pwm-open: chip must be fixnum");
    if (!curry_is_fixnum(av[1])) curry_error("pwm-open: channel must be fixnum");

    int chip    = (int)curry_fixnum(av[0]);
    int channel = (int)curry_fixnum(av[1]);

    /* Export the channel if not already exported */
    char export_path[64], base[64];
    snprintf(export_path, sizeof(export_path),
             "/sys/class/pwm/pwmchip%d/export", chip);
    snprintf(base, sizeof(base),
             "/sys/class/pwm/pwmchip%d/pwm%d", chip, channel);

    char chval[8];
    snprintf(chval, sizeof(chval), "%d", channel);
    /* ignore errors — already exported is fine */
    pwm_write(export_path, chval);

    /* brief wait for sysfs node to appear */
    for (int i = 0; i < 20; i++) {
        if (access(base, F_OK) == 0) break;
        nanosleep(&(struct timespec){0, 10000000}, NULL);
    }
    if (access(base, F_OK) != 0)
        curry_error("pwm-open: sysfs node %s did not appear", base);

    PwmHandle *h = malloc(sizeof(PwmHandle));
    if (!h) curry_error("pwm-open: out of memory");
    h->chip    = chip;
    h->channel = channel;
    snprintf(h->base, sizeof(h->base), "%s", base);

    return wrap_pwm(h);
}

/* (pwm-set! handle period-ns duty-ns) → void */
static curry_val fn_pwm_set(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    PwmHandle *h = get_pwm(av[0], "pwm-set!");
    if (!curry_is_fixnum(av[1])) curry_error("pwm-set!: period-ns must be fixnum");
    if (!curry_is_fixnum(av[2])) curry_error("pwm-set!: duty-ns must be fixnum");

    long period = (long)curry_fixnum(av[1]);
    long duty   = (long)curry_fixnum(av[2]);

    char path[128], val[32];
    snprintf(path, sizeof(path), "%s/period",     h->base);
    snprintf(val,  sizeof(val),  "%ld", period);
    if (pwm_write(path, val) < 0)
        curry_error("pwm-set!: cannot write period: %s", strerror(errno));

    snprintf(path, sizeof(path), "%s/duty_cycle", h->base);
    snprintf(val,  sizeof(val),  "%ld", duty);
    if (pwm_write(path, val) < 0)
        curry_error("pwm-set!: cannot write duty_cycle: %s", strerror(errno));

    return curry_void();
}

/* (pwm-enable! handle) → void */
static curry_val fn_pwm_enable(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    PwmHandle *h = get_pwm(av[0], "pwm-enable!");
    char path[128];
    snprintf(path, sizeof(path), "%s/enable", h->base);
    if (pwm_write(path, "1") < 0)
        curry_error("pwm-enable!: %s", strerror(errno));
    return curry_void();
}

/* (pwm-disable! handle) → void */
static curry_val fn_pwm_disable(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    PwmHandle *h = get_pwm(av[0], "pwm-disable!");
    char path[128];
    snprintf(path, sizeof(path), "%s/enable", h->base);
    if (pwm_write(path, "0") < 0)
        curry_error("pwm-disable!: %s", strerror(errno));
    return curry_void();
}

/* (pwm-close handle) → void — disables and unexports the channel */
static curry_val fn_pwm_close(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    PwmHandle *h = get_pwm(av[0], "pwm-close");

    char path[128];
    snprintf(path, sizeof(path), "%s/enable", h->base);
    pwm_write(path, "0");

    char unexport[64], val[8];
    snprintf(unexport, sizeof(unexport),
             "/sys/class/pwm/pwmchip%d/unexport", h->chip);
    snprintf(val, sizeof(val), "%d", h->channel);
    pwm_write(unexport, val);

    free(h);
    return curry_void();
}

/* (pwm? v) → bool */
static curry_val fn_pwm_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(has_tag(av[0], "pwm"));
}

/* ============================================================
 * Camera (V4L2)
 * ============================================================ */

#define CAM_N_BUFS 4

typedef struct {
    void   *start;
    size_t  length;
} V4LBuf;

typedef struct {
    int      fd;
    uint32_t width, height, pixfmt;
    V4LBuf   bufs[CAM_N_BUFS];
    int      n_bufs;
} Camera;

static curry_val wrap_camera(Camera *c) {
    return curry_cons(curry_intern("camera"), pack_ptr(c));
}
static Camera *get_camera(curry_val v, const char *ctx) {
    if (!has_tag(v, "camera")) curry_error("%s: not a camera handle", ctx);
    return (Camera *)unpack_ptr(curry_cdr(v));
}

/* Map symbolic format name to V4L2 fourcc. */
static uint32_t parse_pixfmt(curry_val sym) {
    if (!curry_is_symbol(sym)) return V4L2_PIX_FMT_YUYV;
    const char *s = curry_symbol(sym);
    if (strcmp(s, "mjpeg") == 0) return V4L2_PIX_FMT_MJPEG;
    if (strcmp(s, "rgb24") == 0) return V4L2_PIX_FMT_RGB24;
    if (strcmp(s, "grey")  == 0) return V4L2_PIX_FMT_GREY;
    if (strcmp(s, "h264")  == 0) return V4L2_PIX_FMT_H264;
    return V4L2_PIX_FMT_YUYV;   /* default */
}

static const char *pixfmt_name(uint32_t fmt) {
    switch (fmt) {
        case V4L2_PIX_FMT_YUYV: return "yuyv";
        case V4L2_PIX_FMT_MJPEG: return "mjpeg";
        case V4L2_PIX_FMT_RGB24: return "rgb24";
        case V4L2_PIX_FMT_GREY:  return "grey";
        case V4L2_PIX_FMT_H264:  return "h264";
        default: return "unknown";
    }
}

/* (camera-open path width height format) → camera-handle
 *   path:   string, e.g. "/dev/video0"
 *   width, height: fixnums
 *   format: symbol 'yuyv | 'mjpeg | 'rgb24 | 'grey | 'h264  (default: 'yuyv) */
static curry_val fn_camera_open(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[0])) curry_error("camera-open: path must be string");
    if (!curry_is_fixnum(av[1])) curry_error("camera-open: width must be fixnum");
    if (!curry_is_fixnum(av[2])) curry_error("camera-open: height must be fixnum");

    const char *path = curry_string(av[0]);
    uint32_t width   = (uint32_t)curry_fixnum(av[1]);
    uint32_t height  = (uint32_t)curry_fixnum(av[2]);
    uint32_t pixfmt  = (ac >= 4) ? parse_pixfmt(av[3]) : V4L2_PIX_FMT_YUYV;

    int fd = open(path, O_RDWR);
    if (fd < 0) curry_error("camera-open: cannot open %s: %s", path, strerror(errno));

    /* Verify capture capability. */
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        close(fd);
        curry_error("camera-open: VIDIOC_QUERYCAP failed: %s", strerror(errno));
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        close(fd);
        curry_error("camera-open: %s is not a capture device", path);
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        close(fd);
        curry_error("camera-open: %s does not support streaming", path);
    }

    /* Set format. */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = width;
    fmt.fmt.pix.height      = height;
    fmt.fmt.pix.pixelformat = pixfmt;
    fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        close(fd);
        curry_error("camera-open: VIDIOC_S_FMT failed: %s", strerror(errno));
    }

    /* Request mmap buffers. */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = CAM_N_BUFS;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        close(fd);
        curry_error("camera-open: VIDIOC_REQBUFS failed: %s", strerror(errno));
    }

    Camera *cam = GC_NEW(Camera);
    cam->fd     = fd;
    cam->width  = fmt.fmt.pix.width;
    cam->height = fmt.fmt.pix.height;
    cam->pixfmt = fmt.fmt.pix.pixelformat;
    cam->n_bufs = (int)req.count;

    for (int i = 0; i < cam->n_bufs; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = (uint32_t)i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            close(fd);
            curry_error("camera-open: VIDIOC_QUERYBUF[%d] failed: %s", i, strerror(errno));
        }
        cam->bufs[i].length = buf.length;
        cam->bufs[i].start  = mmap(NULL, buf.length,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, buf.m.offset);
        if (cam->bufs[i].start == MAP_FAILED) {
            close(fd);
            curry_error("camera-open: mmap[%d] failed: %s", i, strerror(errno));
        }
    }

    /* Queue all buffers. */
    for (int i = 0; i < cam->n_bufs; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = (uint32_t)i;
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            close(fd);
            curry_error("camera-open: VIDIOC_QBUF[%d] failed: %s", i, strerror(errno));
        }
    }

    /* Start streaming. */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        close(fd);
        curry_error("camera-open: VIDIOC_STREAMON failed: %s", strerror(errno));
    }

    return wrap_camera(cam);
}

/* (camera-capture handle) → bytevector of raw frame data */
static curry_val fn_camera_capture(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    Camera *cam = get_camera(av[0], "camera-capture");

    /* Wait for a frame (1-second timeout). */
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(cam->fd, &fds);
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    int r = select(cam->fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0)
        curry_error("camera-capture: timeout waiting for frame");

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(cam->fd, VIDIOC_DQBUF, &buf) < 0)
        curry_error("camera-capture: VIDIOC_DQBUF failed: %s", strerror(errno));

    /* Copy to a bytevector. */
    uint32_t len = buf.bytesused;
    curry_val bv  = curry_make_bytevector((int)len, 0);
    uint8_t  *src = (uint8_t *)cam->bufs[buf.index].start;
    for (uint32_t i = 0; i < len; i++)
        curry_bytevector_set(bv, i, src[i]);

    /* Re-queue the buffer. */
    if (ioctl(cam->fd, VIDIOC_QBUF, &buf) < 0)
        curry_error("camera-capture: VIDIOC_QBUF failed: %s", strerror(errno));

    return bv;
}

/* (camera-close handle) → void */
static curry_val fn_camera_close(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    Camera *cam = get_camera(av[0], "camera-close");
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam->fd, VIDIOC_STREAMOFF, &type);
    for (int i = 0; i < cam->n_bufs; i++)
        munmap(cam->bufs[i].start, cam->bufs[i].length);
    close(cam->fd);
    cam->fd = -1;
    return curry_void();
}

/* (camera? v), (camera-width h), (camera-height h), (camera-format h) */
static curry_val fn_camera_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud; return curry_make_bool(has_tag(av[0], "camera"));
}
static curry_val fn_camera_width(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud; return curry_make_fixnum((int64_t)get_camera(av[0],"camera-width")->width);
}
static curry_val fn_camera_height(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud; return curry_make_fixnum((int64_t)get_camera(av[0],"camera-height")->height);
}
static curry_val fn_camera_format(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    Camera *c = get_camera(av[0], "camera-format");
    return curry_intern(pixfmt_name(c->pixfmt));
}

/* ============================================================
 * UART / serial port (termios)
 * ============================================================ */

static curry_val wrap_uart(int fd) {
    return curry_cons(curry_intern("uart"), pack_fd(fd));
}
static int get_uart(curry_val v, const char *ctx) {
    if (!has_tag(v, "uart")) curry_error("%s: not a uart handle", ctx);
    return unpack_fd(curry_cdr(v));
}

static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:     return B9600;
    }
}

/* (uart-open path baud) → uart-handle
 *   Opens path (e.g. "/dev/ttyAMA0") at given baud rate, 8N1, raw mode. */
static curry_val fn_uart_open(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[0])) curry_error("uart-open: path must be string");
    if (!curry_is_fixnum(av[1])) curry_error("uart-open: baud must be fixnum");

    const char *path = curry_string(av[0]);
    int         baud = (int)curry_fixnum(av[1]);

    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) curry_error("uart-open: cannot open %s: %s", path, strerror(errno));

    /* Clear O_NONBLOCK after open so reads block. */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty;
    if (tcgetattr(fd, &tty) < 0) {
        close(fd); curry_error("uart-open: tcgetattr failed: %s", strerror(errno));
    }

    speed_t speed = baud_to_speed(baud);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    /* 8N1, raw mode (disable all processing). */
    cfmakeraw(&tty);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CSTOPB;   /* 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;  /* no hardware flow control */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) < 0) {
        close(fd); curry_error("uart-open: tcsetattr failed: %s", strerror(errno));
    }
    tcflush(fd, TCIOFLUSH);
    return wrap_uart(fd);
}

/* (uart-read handle n) → bytevector  (blocks until n bytes received) */
static curry_val fn_uart_read(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int fd = get_uart(av[0], "uart-read");
    if (!curry_is_fixnum(av[1])) curry_error("uart-read: n must be fixnum");
    int n = (int)curry_fixnum(av[1]);
    if (n <= 0 || n > 65536) curry_error("uart-read: n out of range [1,65536]");

    uint8_t *buf = malloc((size_t)n);
    int total = 0;
    while (total < n) {
        int r = (int)read(fd, buf + total, (size_t)(n - total));
        if (r < 0) { free(buf); curry_error("uart-read: %s", strerror(errno)); }
        if (r == 0) break;
        total += r;
    }
    curry_val bv = curry_make_bytevector(total, 0);
    for (int i = 0; i < total; i++)
        curry_bytevector_set(bv, (uint32_t)i, buf[i]);
    free(buf);
    return bv;
}

/* (uart-write handle bytevector) → void */
static curry_val fn_uart_write(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int fd = get_uart(av[0], "uart-write");
    if (!curry_is_bytevector(av[1])) curry_error("uart-write: must be bytevector");
    int n = (int)curry_bytevector_length(av[1]);
    uint8_t *buf = malloc((size_t)n);
    for (int i = 0; i < n; i++)
        buf[i] = curry_bytevector_ref(av[1], (uint32_t)i);
    int r = (int)write(fd, buf, (size_t)n);
    free(buf);
    if (r < 0) curry_error("uart-write: %s", strerror(errno));
    return curry_void();
}

/* (uart-read-line handle timeout-ms) → string | #f
 *   Reads until '\n' or timeout.  Returns string without the newline,
 *   or #f on timeout. */
static curry_val fn_uart_read_line(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int fd      = get_uart(av[0], "uart-read-line");
    int timeout = (ac >= 2 && curry_is_fixnum(av[1])) ? (int)curry_fixnum(av[1]) : 1000;

    char buf[4096];
    int  pos = 0;

    while (pos < (int)sizeof(buf) - 1) {
        fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
        struct timeval tv = { .tv_sec = timeout / 1000,
                              .tv_usec = (timeout % 1000) * 1000 };
        int r = select(fd + 1, &fds, NULL, NULL, &tv);
        if (r <= 0) {
            /* timeout */
            if (pos == 0) return curry_make_bool(0);
            break;
        }
        char c;
        if (read(fd, &c, 1) <= 0) break;
        if (c == '\n') break;
        if (c != '\r') buf[pos++] = c;
    }
    buf[pos] = '\0';
    return curry_make_string(buf);
}

/* (uart-available? handle) → fixnum (bytes waiting in input buffer) */
static curry_val fn_uart_available(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int fd = get_uart(av[0], "uart-available?");
    int n  = 0;
    ioctl(fd, FIONREAD, &n);
    return curry_make_fixnum((int64_t)n);
}

/* (uart-close handle) → void */
static curry_val fn_uart_close(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    close(get_uart(av[0], "uart-close"));
    return curry_void();
}

/* (uart? v) */
static curry_val fn_uart_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud; return curry_make_bool(has_tag(av[0], "uart"));
}

/* ============================================================
 * 1-Wire (sysfs DS18B20)
 * ============================================================ */

/* (w1-devices) → list of device-path strings
 *   Returns paths under /sys/bus/w1/devices/ excluding bus master entries. */
static curry_val fn_w1_devices(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    DIR *dir = opendir("/sys/bus/w1/devices");
    if (!dir) return curry_nil();

    curry_val head = curry_nil();
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        /* Skip . .. and master devices (w1_bus_master*) */
        if (ent->d_name[0] == '.') continue;
        if (strncmp(ent->d_name, "w1_bus_master", 13) == 0) continue;
        char path[256];
        snprintf(path, sizeof(path), "/sys/bus/w1/devices/%s", ent->d_name);
        head = curry_cons(curry_make_string(path), head);
    }
    closedir(dir);
    return head;
}

/* (w1-temperature path) → flonum (°C)
 *   Parses the w1_slave sysfs file for a DS18B20 sensor.
 *   File format: "... : crc=XX YES\n... t=21312\n" */
static curry_val fn_w1_temperature(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[0])) curry_error("w1-temperature: path must be string");

    char slave_path[512];
    snprintf(slave_path, sizeof(slave_path), "%s/w1_slave", curry_string(av[0]));

    FILE *f = fopen(slave_path, "r");
    if (!f) curry_error("w1-temperature: cannot open %s: %s", slave_path, strerror(errno));

    char line1[128], line2[128];
    if (!fgets(line1, sizeof(line1), f) || !fgets(line2, sizeof(line2), f)) {
        fclose(f);
        curry_error("w1-temperature: short read from %s", slave_path);
    }
    fclose(f);

    if (!strstr(line1, "YES"))
        curry_error("w1-temperature: CRC check failed for %s", curry_string(av[0]));

    char *t_pos = strstr(line2, "t=");
    if (!t_pos)
        curry_error("w1-temperature: cannot find t= in %s", slave_path);

    long raw = strtol(t_pos + 2, NULL, 10);
    return curry_make_flonum((double)raw / 1000.0);
}

/* (w1-raw path) → string (raw content of w1_slave file) */
static curry_val fn_w1_raw(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[0])) curry_error("w1-raw: path must be string");

    char slave_path[512];
    snprintf(slave_path, sizeof(slave_path), "%s/w1_slave", curry_string(av[0]));

    FILE *f = fopen(slave_path, "r");
    if (!f) curry_error("w1-raw: cannot open %s: %s", slave_path, strerror(errno));

    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return curry_make_string(buf);
}

/* ============================================================
 * Hardware watchdog (/dev/watchdog)
 * ============================================================ */

static curry_val wrap_watchdog(int fd) {
    return curry_cons(curry_intern("watchdog"), pack_fd(fd));
}
static int get_watchdog(curry_val v, const char *ctx) {
    if (!has_tag(v, "watchdog")) curry_error("%s: not a watchdog handle", ctx);
    return unpack_fd(curry_cdr(v));
}

/* (watchdog-open [timeout-secs]) → watchdog-handle
 *   Opens /dev/watchdog.  Optional timeout sets the hardware timeout.
 *   The watchdog starts immediately; call watchdog-kick periodically. */
static curry_val fn_watchdog_open(int ac, curry_val *av, void *ud) {
    (void)ud;
    int fd = open("/dev/watchdog", O_RDWR);
    if (fd < 0) curry_error("watchdog-open: cannot open /dev/watchdog: %s", strerror(errno));

    if (ac >= 1 && curry_is_fixnum(av[0])) {
        int timeout = (int)curry_fixnum(av[0]);
        if (ioctl(fd, WDIOC_SETTIMEOUT, &timeout) < 0) {
            /* Not all watchdog drivers support SETTIMEOUT — warn but continue. */
        }
    }
    return wrap_watchdog(fd);
}

/* (watchdog-kick handle) → void  (pet/feed the watchdog to prevent reboot) */
static curry_val fn_watchdog_kick(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int fd = get_watchdog(av[0], "watchdog-kick");
    int dummy = 0;
    ioctl(fd, WDIOC_KEEPALIVE, &dummy);
    return curry_void();
}

/* (watchdog-timeout handle) → fixnum (current hardware timeout in seconds) */
static curry_val fn_watchdog_timeout(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int fd = get_watchdog(av[0], "watchdog-timeout");
    int timeout = 0;
    ioctl(fd, WDIOC_GETTIMEOUT, &timeout);
    return curry_make_fixnum((int64_t)timeout);
}

/* (watchdog-close handle) → void
 *   Writes magic 'V' byte to disarm the watchdog before closing, so the
 *   system does not reboot.  If you want a guaranteed reboot, just let the
 *   process exit without calling this. */
static curry_val fn_watchdog_close(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int fd = get_watchdog(av[0], "watchdog-close");
    write(fd, "V", 1);   /* magic disarm */
    close(fd);
    return curry_void();
}

/* (watchdog? v) */
static curry_val fn_watchdog_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud; return curry_make_bool(has_tag(av[0], "watchdog"));
}

/* ============================================================
 * Board detection (sysfs / procfs)
 * ============================================================ */

static curry_val read_first_line(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return curry_make_bool(0);
    char buf[256];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return curry_make_bool(0); }
    fclose(f);
    /* Trim trailing newline and null bytes. */
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == '\0'))
        buf[--n] = '\0';
    return curry_make_string(buf);
}

/* (rpi-model) → string, e.g. "Raspberry Pi 4 Model B Rev 1.4"
 *   Reads /proc/device-tree/model.  Returns #f if unavailable. */
static curry_val fn_rpi_model(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    /* /proc/device-tree/model is a null-terminated string, possibly not newline terminated. */
    FILE *f = fopen("/proc/device-tree/model", "r");
    if (!f) {
        /* Fallback: parse /proc/cpuinfo for "Model" line. */
        f = fopen("/proc/cpuinfo", "r");
        if (!f) return curry_make_bool(0);
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "Model", 5) == 0) {
                fclose(f);
                char *colon = strchr(line, ':');
                if (!colon) return curry_make_bool(0);
                colon++;
                while (*colon == ' ' || *colon == '\t') colon++;
                size_t n = strlen(colon);
                while (n > 0 && (colon[n-1] == '\n' || colon[n-1] == '\r')) colon[--n] = '\0';
                return curry_make_string(colon);
            }
        }
        fclose(f);
        return curry_make_bool(0);
    }
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    /* Strip embedded null bytes from device-tree string. */
    for (size_t i = 0; i < n; i++) if (buf[i] == '\0') buf[i] = ' ';
    while (n > 0 && (buf[n-1] == ' ' || buf[n-1] == '\n')) buf[--n] = '\0';
    return curry_make_string(buf);
}

/* (rpi-serial) → string (board serial number from /proc/cpuinfo) */
static curry_val fn_rpi_serial(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return curry_make_bool(0);
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Serial", 6) == 0) {
            fclose(f);
            char *colon = strchr(line, ':');
            if (!colon) return curry_make_bool(0);
            colon++;
            while (*colon == ' ' || *colon == '\t') colon++;
            size_t n = strlen(colon);
            while (n > 0 && (colon[n-1] == '\n' || colon[n-1] == '\r')) colon[--n] = '\0';
            return curry_make_string(colon);
        }
    }
    fclose(f);
    return curry_make_bool(0);
}

/* (rpi-memory-mb) → fixnum (total RAM in MiB from /proc/meminfo) */
static curry_val fn_rpi_memory_mb(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return curry_make_fixnum(0);
    char line[256];
    long kB = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %ld kB", &kB);
            break;
        }
    }
    fclose(f);
    return curry_make_fixnum((int64_t)(kB / 1024));
}

/* (rpi-os-info) → alist ((kernel . "6.1.21-v8+") (distro . "Raspbian GNU/Linux 12") ...)
 *   Reads /proc/version and /etc/os-release. */
static curry_val fn_rpi_os_info(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    curry_val result = curry_nil();

    /* Kernel version: first word after "Linux version " in /proc/version. */
    FILE *f = fopen("/proc/version", "r");
    if (f) {
        char line[512];
        if (fgets(line, sizeof(line), f)) {
            char *kv = strstr(line, "Linux version ");
            if (kv) {
                kv += 14;
                char *sp = strchr(kv, ' ');
                if (sp) *sp = '\0';
                result = curry_cons(
                    curry_cons(curry_intern("kernel"), curry_make_string(kv)),
                    result);
            }
        }
        fclose(f);
    }

    /* Distro: PRETTY_NAME from /etc/os-release. */
    f = fopen("/etc/os-release", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                char *val = line + 12;
                if (*val == '"') val++;
                size_t n = strlen(val);
                while (n > 0 && (val[n-1] == '"' || val[n-1] == '\n' || val[n-1] == '\r'))
                    val[--n] = '\0';
                result = curry_cons(
                    curry_cons(curry_intern("distro"), curry_make_string(val)),
                    result);
                break;
            }
        }
        fclose(f);
    }

    return result;
}

/* ============================================================
 * Module init
 * ============================================================ */

void curry_module_init(CurryVM *vm) {
#define DEF(n, f, mn, mx) curry_define_fn(vm, n, f, mn, mx, NULL)
    /* GPIO */
    DEF("gpio-open",       fn_gpio_open,      3, 3);
    DEF("gpio-read",       fn_gpio_read,      1, 1);
    DEF("gpio-write",      fn_gpio_write,     2, 2);
    DEF("gpio-close",      fn_gpio_close,     1, 1);
    DEF("gpio?",           fn_gpio_p,         1, 1);
    DEF("gpio-wait-edge",  fn_gpio_wait_edge, 1, 2);
    DEF("gpio-watch",      fn_gpio_watch,     2, 2);
    DEF("gpio-unwatch",    fn_gpio_unwatch,   1, 1);
    DEF("watcher?",        fn_watcher_p,      1, 1);
    /* I2C */
    DEF("i2c-open",        fn_i2c_open,     1, 1);
    DEF("i2c-read",        fn_i2c_read,     4, 4);
    DEF("i2c-write",       fn_i2c_write,    4, 4);
    DEF("i2c-close",       fn_i2c_close,    1, 1);
    DEF("i2c?",            fn_i2c_p,        1, 1);
    /* SPI */
    DEF("spi-open",        fn_spi_open,     4, 4);
    DEF("spi-transfer",    fn_spi_transfer, 2, 2);
    DEF("spi-close",       fn_spi_close,    1, 1);
    DEF("spi?",            fn_spi_p,        1, 1);
    /* PWM */
    DEF("pwm-open",        fn_pwm_open,     2, 2);
    DEF("pwm-set!",        fn_pwm_set,      3, 3);
    DEF("pwm-enable!",     fn_pwm_enable,   1, 1);
    DEF("pwm-disable!",    fn_pwm_disable,  1, 1);
    DEF("pwm-close",       fn_pwm_close,    1, 1);
    DEF("pwm?",            fn_pwm_p,        1, 1);
    /* Camera (V4L2) */
    DEF("camera-open",     fn_camera_open,    3, 4);
    DEF("camera-capture",  fn_camera_capture, 1, 1);
    DEF("camera-close",    fn_camera_close,   1, 1);
    DEF("camera?",         fn_camera_p,       1, 1);
    DEF("camera-width",    fn_camera_width,   1, 1);
    DEF("camera-height",   fn_camera_height,  1, 1);
    DEF("camera-format",   fn_camera_format,  1, 1);
    /* UART */
    DEF("uart-open",       fn_uart_open,      2, 2);
    DEF("uart-read",       fn_uart_read,      2, 2);
    DEF("uart-write",      fn_uart_write,     2, 2);
    DEF("uart-read-line",  fn_uart_read_line, 1, 2);
    DEF("uart-available?", fn_uart_available, 1, 1);
    DEF("uart-close",      fn_uart_close,     1, 1);
    DEF("uart?",           fn_uart_p,         1, 1);
    /* 1-Wire */
    DEF("w1-devices",     fn_w1_devices,     0, 0);
    DEF("w1-temperature", fn_w1_temperature, 1, 1);
    DEF("w1-raw",         fn_w1_raw,         1, 1);
    /* Watchdog */
    DEF("watchdog-open",    fn_watchdog_open,    0, 1);
    DEF("watchdog-kick",    fn_watchdog_kick,    1, 1);
    DEF("watchdog-timeout", fn_watchdog_timeout, 1, 1);
    DEF("watchdog-close",   fn_watchdog_close,   1, 1);
    DEF("watchdog?",        fn_watchdog_p,       1, 1);
    /* Board info */
    DEF("rpi-model",     fn_rpi_model,     0, 0);
    DEF("rpi-serial",    fn_rpi_serial,    0, 0);
    DEF("rpi-memory-mb", fn_rpi_memory_mb, 0, 0);
    DEF("rpi-os-info",   fn_rpi_os_info,   0, 0);
#undef DEF
}
