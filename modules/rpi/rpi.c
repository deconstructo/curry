/* rpi.c — Raspberry Pi / Linux embedded hardware module for Curry Scheme.
 *
 * Provides GPIO (via libgpiod), I2C, SPI, and PWM (sysfs) interfaces.
 * Linux-only.  Not supported on macOS or other non-Linux platforms.
 *
 * Handles are tagged pairs:
 *   ("gpio"  . bv)  — gpiod_line*
 *   ("i2c"   . bv)  — int fd
 *   ("spi"   . bv)  — int fd
 *   ("pwm"   . bv)  — #(chip channel sysfs-path)
 *
 * Dependencies:
 *   GPIO: libgpiod  (sudo apt install libgpiod-dev)
 *   I2C:  kernel headers only  (linux/i2c-dev.h)
 *   SPI:  kernel headers only  (linux/spi/spidev.h)
 *   PWM:  sysfs — no extra library
 */

#include <curry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>

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
    } else {
        gpiod_chip_close(chip);
        curry_error("gpio-open: direction must be 'input or 'output, got '%s'", dir);
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
        usleep(10000);
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

/* ---- Module init ---- */

void curry_module_init(CurryVM *vm) {
#define DEF(n, f, mn, mx) curry_define_fn(vm, n, f, mn, mx, NULL)
    /* GPIO */
    DEF("gpio-open",       fn_gpio_open,    3, 3);
    DEF("gpio-read",       fn_gpio_read,    1, 1);
    DEF("gpio-write",      fn_gpio_write,   2, 2);
    DEF("gpio-close",      fn_gpio_close,   1, 1);
    DEF("gpio?",           fn_gpio_p,       1, 1);
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
#undef DEF
}
