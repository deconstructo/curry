/* posix.c — POSIX filesystem/process bindings for Curry Scheme (SRFI-170
 * subset), plus SRFI-112 environment inquiry. Pure libc, no extra library
 * dependency, macOS/Linux portable.
 *
 * Provides file-info (stat/lstat) + type predicates, directory creation/
 * listing/removal, symlinks/hardlinks/rename, file mode/owner/times,
 * process state (cwd, umask, pid, uid/gid, niceness), user/group database
 * lookups, monotonic/wall-clock time, environment-variable mutation, a
 * terminal? predicate, and (SRFI-112) implementation/OS/machine identity
 * queries via uname(2)/gethostname(2).
 *
 * Deliberately out of scope for this first pass (see docs/reference/
 * module-posix.md): posix-error? type introspection (would need every
 * call site here to tag its errno consistently — a bigger design task),
 * open-file/fd->port (curry already has file ports; a full O_* flag API
 * is its own task), create-fifo, temp-file helpers, file-space,
 * make-directory-files-generator (needs a generator protocol curry
 * doesn't have), and the SRFI's port-type/buffering-mode constants.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <curry.h>
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>

/* ---- pointer packing for opaque handles (DIR*), same idiom as rpi.c/sync.c --- */

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

/* Overwrite a packed-pointer bytevector with all-zero bytes, marking the
 * handle closed — unpack_ptr on it thereafter yields NULL rather than the
 * stale pointer, so a second close/read after close raises a clean Scheme
 * error instead of handing a freed DIR* back to libc (double-close/UAF). */
static void clear_ptr(curry_val bv) {
    for (size_t i = 0; i < sizeof(void *); i++)
        curry_bytevector_set(bv, (uint32_t)i, 0);
}

static int has_tag(curry_val v, const char *tag) {
    return curry_is_pair(v) && curry_is_symbol(curry_car(v)) &&
           strcmp(curry_symbol(curry_car(v)), tag) == 0;
}

static void posix_error(const char *fn) {
    curry_error("posix: %s: %s", fn, strerror(errno));
}

static const char *req_string(curry_val v, const char *fn) {
    if (!curry_is_string(v)) curry_error("posix: %s: expected a string path", fn);
    return curry_string(v);
}

/* curry_fixnum() has no type check of its own (see src/api.c) — it just
 * shifts whatever bit pattern it's handed, so calling it on a bignum,
 * string, or other non-fixnum value silently returns garbage derived from
 * the value's pointer bits instead of erroring. Every numeric argument in
 * this module (modes, uids, gids, lengths, time values, …) goes through
 * this check first so a caller mistake — e.g. passing a bignum that
 * overflowed fixnum range — is a clean Scheme error, not a garbage mode/
 * owner/timestamp silently applied to the filesystem. */
static intptr_t req_fixnum(curry_val v, const char *fn) {
    if (!curry_is_fixnum(v)) curry_error("posix: %s: expected an exact integer", fn);
    return curry_fixnum(v);
}

/* ---- file-info: a tagged vector #(file-info dev ino mode nlink uid gid
 *      rdev size blksize blocks atime mtime ctime) ------------------------ */

enum {
    FI_TAG = 0, FI_DEVICE, FI_INODE, FI_MODE, FI_NLINKS, FI_UID, FI_GID,
    FI_RDEV, FI_SIZE, FI_BLKSIZE, FI_BLOCKS, FI_ATIME, FI_MTIME, FI_CTIME,
    FI_LEN
};

static curry_val make_file_info(const struct stat *st) {
    curry_val v = curry_make_vector(FI_LEN, curry_void());
    curry_vector_set(v, FI_TAG,     curry_make_symbol("file-info"));
    curry_vector_set(v, FI_DEVICE,  curry_make_fixnum((intptr_t)st->st_dev));
    curry_vector_set(v, FI_INODE,   curry_make_fixnum((intptr_t)st->st_ino));
    curry_vector_set(v, FI_MODE,    curry_make_fixnum((intptr_t)st->st_mode));
    curry_vector_set(v, FI_NLINKS,  curry_make_fixnum((intptr_t)st->st_nlink));
    curry_vector_set(v, FI_UID,     curry_make_fixnum((intptr_t)st->st_uid));
    curry_vector_set(v, FI_GID,     curry_make_fixnum((intptr_t)st->st_gid));
    curry_vector_set(v, FI_RDEV,    curry_make_fixnum((intptr_t)st->st_rdev));
    curry_vector_set(v, FI_SIZE,    curry_make_fixnum((intptr_t)st->st_size));
    curry_vector_set(v, FI_BLKSIZE, curry_make_fixnum((intptr_t)st->st_blksize));
    curry_vector_set(v, FI_BLOCKS,  curry_make_fixnum((intptr_t)st->st_blocks));
    curry_vector_set(v, FI_ATIME,   curry_make_fixnum((intptr_t)st->st_atime));
    curry_vector_set(v, FI_MTIME,   curry_make_fixnum((intptr_t)st->st_mtime));
    curry_vector_set(v, FI_CTIME,   curry_make_fixnum((intptr_t)st->st_ctime));
    return v;
}

static int is_file_info(curry_val v) {
    if (!curry_is_vector(v) || curry_vector_length(v) != FI_LEN) return 0;
    curry_val tag = curry_vector_ref(v, FI_TAG);
    return curry_is_symbol(tag) && strcmp(curry_symbol(tag), "file-info") == 0;
}

static curry_val checked_file_info(curry_val v, const char *fn) {
    if (!is_file_info(v)) curry_error("posix: %s: not a file-info object", fn);
    return v;
}

static curry_val fn_file_info(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *path = req_string(av[0], "file-info");
    bool follow = ac < 2 || curry_is_true(av[1]);
    struct stat st;
    int r = follow ? stat(path, &st) : lstat(path, &st);
    if (r < 0) posix_error("file-info");
    return make_file_info(&st);
}

static curry_val fn_file_info_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(is_file_info(av[0]));
}

/* Generic accessor: ud is the FI_* field index cast to a pointer. */
static curry_val fn_file_info_ref(int ac, curry_val *av, void *ud) {
    (void)ac;
    curry_val v = checked_file_info(av[0], "file-info accessor");
    return curry_vector_ref(v, (uint32_t)(intptr_t)ud);
}

/* Generic type predicate: ud is the S_IFMT bits to compare st_mode against. */
static curry_val fn_file_info_type_p(int ac, curry_val *av, void *ud) {
    (void)ac;
    curry_val v = checked_file_info(av[0], "file-info type predicate");
    mode_t mode = (mode_t)curry_fixnum(curry_vector_ref(v, FI_MODE));
    mode_t want = (mode_t)(intptr_t)ud;
    return curry_make_bool((mode & S_IFMT) == want);
}

/* ---- directories -------------------------------------------------------- */

static curry_val fn_create_directory(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *path = req_string(av[0], "create-directory");
    mode_t mode = ac >= 2 ? (mode_t)req_fixnum(av[1], "create-directory") : 0777;
    if (mkdir(path, mode) < 0) posix_error("create-directory");
    return curry_void();
}

static curry_val fn_delete_directory(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *path = req_string(av[0], "delete-directory");
    if (rmdir(path) < 0) posix_error("delete-directory");
    return curry_void();
}

static curry_val fn_directory_files(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *path = req_string(av[0], "directory-files");
    bool dotfiles = ac >= 2 && curry_is_true(av[1]);
    DIR *d = opendir(path);
    if (!d) posix_error("directory-files");
    /* No public curry_set_cdr! exists to append in place, so build the list
     * by prepending (O(1) per entry) and reverse once at the end. */
    struct dirent *e;
    curry_val acc = curry_nil();
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (!dotfiles && e->d_name[0] == '.') continue;
        acc = curry_make_pair(curry_make_string(e->d_name), acc);
    }
    closedir(d);
    curry_val rev = curry_nil();
    while (!curry_is_nil(acc)) {
        rev = curry_make_pair(curry_car(acc), rev);
        acc = curry_cdr(acc);
    }
    return rev;
}

static curry_val fn_open_directory(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *path = req_string(av[0], "open-directory");
    DIR *d = opendir(path);
    if (!d) posix_error("open-directory");
    return curry_make_pair(curry_make_symbol("directory-stream"), pack_ptr(d));
}

static curry_val fn_read_directory(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!has_tag(av[0], "directory-stream"))
        curry_error("posix: read-directory: not a directory stream");
    DIR *d = (DIR *)unpack_ptr(curry_cdr(av[0]));
    if (!d) curry_error("posix: read-directory: directory stream already closed");
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        return curry_make_string(e->d_name);
    }
    return curry_eof();
}

static curry_val fn_close_directory(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!has_tag(av[0], "directory-stream"))
        curry_error("posix: close-directory: not a directory stream");
    curry_val bv = curry_cdr(av[0]);
    DIR *d = (DIR *)unpack_ptr(bv);
    if (!d) curry_error("posix: close-directory: directory stream already closed");
    clear_ptr(bv);
    closedir(d);
    return curry_void();
}

/* ---- links / rename / paths --------------------------------------------- */

static curry_val fn_rename_file(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *from = req_string(av[0], "rename-file");
    const char *to   = req_string(av[1], "rename-file");
    if (rename(from, to) < 0) posix_error("rename-file");
    return curry_void();
}

static curry_val fn_create_hard_link(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *from = req_string(av[0], "create-hard-link");
    const char *to   = req_string(av[1], "create-hard-link");
    if (link(from, to) < 0) posix_error("create-hard-link");
    return curry_void();
}

static curry_val fn_create_symlink(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *target = req_string(av[0], "create-symlink");
    const char *linkpath = req_string(av[1], "create-symlink");
    if (symlink(target, linkpath) < 0) posix_error("create-symlink");
    return curry_void();
}

static curry_val fn_read_symlink(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *path = req_string(av[0], "read-symlink");
    /* readlink(2) neither NUL-terminates nor reports truncation except by
     * returning exactly the buffer size given — indistinguishable from "the
     * target happens to be exactly that long". Loop, doubling the buffer,
     * until a read comes back strictly smaller than what was offered. */
    size_t cap = 1024;
    for (;;) {
        char *buf = (char *)malloc(cap);
        if (!buf) curry_error("posix: read-symlink: out of memory");
        ssize_t n = readlink(path, buf, cap);
        if (n < 0) { free(buf); posix_error("read-symlink"); }
        if ((size_t)n < cap) {
            curry_val r = curry_make_string_n(buf, (uint32_t)n);
            free(buf);
            return r;
        }
        free(buf);
        cap *= 2;
    }
}

static curry_val fn_real_path(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *path = req_string(av[0], "real-path");
    char buf[4096];
    if (realpath(path, buf) == NULL) posix_error("real-path");
    return curry_make_string(buf);
}

static curry_val fn_truncate_file(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *path = req_string(av[0], "truncate-file");
    off_t len = (off_t)req_fixnum(av[1], "truncate-file");
    if (truncate(path, len) < 0) posix_error("truncate-file");
    return curry_void();
}

static curry_val fn_set_file_mode(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *path = req_string(av[0], "set-file-mode");
    mode_t mode = (mode_t)req_fixnum(av[1], "set-file-mode");
    if (chmod(path, mode) < 0) posix_error("set-file-mode");
    return curry_void();
}

static curry_val fn_set_file_owner(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *path = req_string(av[0], "set-file-owner");
    uid_t uid = (uid_t)req_fixnum(av[1], "set-file-owner");
    gid_t gid = ac >= 3 ? (gid_t)req_fixnum(av[2], "set-file-owner") : (gid_t)-1;
    if (chown(path, uid, gid) < 0) posix_error("set-file-owner");
    return curry_void();
}

static double req_time_seconds(curry_val v, const char *fn) {
    if (curry_is_float(v)) return curry_float(v);
    return (double)req_fixnum(v, fn);
}

static curry_val fn_set_file_times(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *path = req_string(av[0], "set-file-times");
    struct timespec times[2];
    if (ac >= 2 && curry_is_true(av[1])) {
        times[0].tv_sec = (time_t)req_time_seconds(av[1], "set-file-times");
        times[0].tv_nsec = 0;
    } else {
        times[0].tv_nsec = UTIME_NOW;
    }
    if (ac >= 3 && curry_is_true(av[2])) {
        times[1].tv_sec = (time_t)req_time_seconds(av[2], "set-file-times");
        times[1].tv_nsec = 0;
    } else {
        times[1].tv_nsec = UTIME_NOW;
    }
    if (utimensat(AT_FDCWD, path, times, 0) < 0) posix_error("set-file-times");
    return curry_void();
}

/* ---- process state -------------------------------------------------------- */

static curry_val fn_umask(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    mode_t old = umask(0);
    umask(old);
    return curry_make_fixnum((intptr_t)old);
}

static curry_val fn_set_umask(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    umask((mode_t)req_fixnum(av[0], "set-umask!"));
    return curry_void();
}

static curry_val fn_current_directory(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    char buf[4096];
    if (getcwd(buf, sizeof(buf)) == NULL) posix_error("current-directory");
    return curry_make_string(buf);
}

static curry_val fn_set_current_directory(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *path = req_string(av[0], "set-current-directory!");
    if (chdir(path) < 0) posix_error("set-current-directory!");
    return curry_void();
}

static curry_val fn_pid(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return curry_make_fixnum((intptr_t)getpid());
}

static curry_val fn_nice(int ac, curry_val *av, void *ud) {
    (void)ud;
    errno = 0;
    if (ac == 0) {
        int cur = getpriority(PRIO_PROCESS, 0);
        if (cur == -1 && errno != 0) posix_error("nice");
        return curry_make_fixnum(cur);
    }
    int incr = (int)req_fixnum(av[0], "nice");
    int cur = getpriority(PRIO_PROCESS, 0);
    if (cur == -1 && errno != 0) posix_error("nice");
    int want = cur + incr;
    if (setpriority(PRIO_PROCESS, 0, want) < 0) posix_error("nice");
    return curry_make_fixnum(want);
}

static curry_val fn_user_uid(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return curry_make_fixnum((intptr_t)getuid());
}
static curry_val fn_user_gid(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return curry_make_fixnum((intptr_t)getgid());
}
static curry_val fn_user_effective_uid(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return curry_make_fixnum((intptr_t)geteuid());
}
static curry_val fn_user_effective_gid(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return curry_make_fixnum((intptr_t)getegid());
}

static curry_val fn_user_supplementary_gids(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    int n = getgroups(0, NULL);
    if (n < 0) posix_error("user-supplementary-gids");
    if (n == 0) return curry_nil();
    gid_t *groups = (gid_t *)malloc((size_t)n * sizeof(gid_t));
    if (!groups) curry_error("posix: user-supplementary-gids: out of memory");
    int got = getgroups(n, groups);
    if (got < 0) { free(groups); posix_error("user-supplementary-gids"); }
    curry_val acc = curry_nil();
    for (int i = got - 1; i >= 0; i--)
        acc = curry_make_pair(curry_make_fixnum((intptr_t)groups[i]), acc);
    free(groups);
    return acc;
}

/* ---- user/group database ------------------------------------------------- */

enum { UI_TAG = 0, UI_NAME, UI_UID, UI_GID, UI_HOME, UI_SHELL, UI_FULLNAME, UI_LEN };
enum { GI_TAG = 0, GI_NAME, GI_GID, GI_LEN };

static curry_val make_user_info(const struct passwd *pw) {
    curry_val v = curry_make_vector(UI_LEN, curry_void());
    curry_vector_set(v, UI_TAG,      curry_make_symbol("user-info"));
    curry_vector_set(v, UI_NAME,     curry_make_string(pw->pw_name ? pw->pw_name : ""));
    curry_vector_set(v, UI_UID,      curry_make_fixnum((intptr_t)pw->pw_uid));
    curry_vector_set(v, UI_GID,      curry_make_fixnum((intptr_t)pw->pw_gid));
    curry_vector_set(v, UI_HOME,     curry_make_string(pw->pw_dir ? pw->pw_dir : ""));
    curry_vector_set(v, UI_SHELL,    curry_make_string(pw->pw_shell ? pw->pw_shell : ""));
    curry_vector_set(v, UI_FULLNAME, curry_make_string(pw->pw_gecos ? pw->pw_gecos : ""));
    return v;
}

static int is_user_info(curry_val v) {
    if (!curry_is_vector(v) || curry_vector_length(v) != UI_LEN) return 0;
    curry_val tag = curry_vector_ref(v, UI_TAG);
    return curry_is_symbol(tag) && strcmp(curry_symbol(tag), "user-info") == 0;
}

static curry_val fn_user_info(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    struct passwd pwbuf, *pw = NULL;
    char buf[4096];
    int rc;
    if (curry_is_fixnum(av[0]))
        rc = getpwuid_r((uid_t)curry_fixnum(av[0]), &pwbuf, buf, sizeof(buf), &pw);
    else
        rc = getpwnam_r(req_string(av[0], "user-info"), &pwbuf, buf, sizeof(buf), &pw);
    if (rc != 0) { errno = rc; posix_error("user-info"); }
    if (!pw) curry_error("posix: user-info: no such user");
    return make_user_info(pw);
}

static curry_val fn_user_info_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(is_user_info(av[0]));
}

static curry_val fn_user_info_ref(int ac, curry_val *av, void *ud) {
    (void)ac;
    if (!is_user_info(av[0])) curry_error("posix: user-info accessor: not a user-info object");
    return curry_vector_ref(av[0], (uint32_t)(intptr_t)ud);
}

static curry_val make_group_info(const struct group *gr) {
    curry_val v = curry_make_vector(GI_LEN, curry_void());
    curry_vector_set(v, GI_TAG,  curry_make_symbol("group-info"));
    curry_vector_set(v, GI_NAME, curry_make_string(gr->gr_name ? gr->gr_name : ""));
    curry_vector_set(v, GI_GID,  curry_make_fixnum((intptr_t)gr->gr_gid));
    return v;
}

static int is_group_info(curry_val v) {
    if (!curry_is_vector(v) || curry_vector_length(v) != GI_LEN) return 0;
    curry_val tag = curry_vector_ref(v, GI_TAG);
    return curry_is_symbol(tag) && strcmp(curry_symbol(tag), "group-info") == 0;
}

static curry_val fn_group_info(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    struct group grbuf, *gr = NULL;
    char buf[4096];
    int rc;
    if (curry_is_fixnum(av[0]))
        rc = getgrgid_r((gid_t)curry_fixnum(av[0]), &grbuf, buf, sizeof(buf), &gr);
    else
        rc = getgrnam_r(req_string(av[0], "group-info"), &grbuf, buf, sizeof(buf), &gr);
    if (rc != 0) { errno = rc; posix_error("group-info"); }
    if (!gr) curry_error("posix: group-info: no such group");
    return make_group_info(gr);
}

static curry_val fn_group_info_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(is_group_info(av[0]));
}

static curry_val fn_group_info_ref(int ac, curry_val *av, void *ud) {
    (void)ac;
    if (!is_group_info(av[0])) curry_error("posix: group-info accessor: not a group-info object");
    return curry_vector_ref(av[0], (uint32_t)(intptr_t)ud);
}

/* ---- time ----------------------------------------------------------------- */

static curry_val fn_posix_time(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return curry_make_float((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
}

static curry_val fn_monotonic_time(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return curry_make_float((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
}

/* ---- environment / terminal ------------------------------------------------ */

static curry_val fn_set_environment_variable(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *name = req_string(av[0], "set-environment-variable!");
    const char *val  = req_string(av[1], "set-environment-variable!");
    if (setenv(name, val, 1) < 0) posix_error("set-environment-variable!");
    return curry_void();
}

static curry_val fn_delete_environment_variable(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const char *name = req_string(av[0], "delete-environment-variable!");
    if (unsetenv(name) < 0) posix_error("delete-environment-variable!");
    return curry_void();
}

static curry_val fn_terminal_p(int ac, curry_val *av, void *ud) {
    (void)ud;
    int fd;
    if (ac == 0) fd = STDIN_FILENO;
    else if (curry_is_fixnum(av[0])) fd = (int)curry_fixnum(av[0]);
    else fd = curry_port_fd(av[0]);
    if (fd < 0) return curry_make_bool(false);
    return curry_make_bool(isatty(fd) != 0);
}

/* ---- SRFI-112: environment inquiry ---------------------------------------- */

static curry_val fn_implementation_name(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return curry_make_string("curry");
}

static curry_val fn_implementation_version(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return curry_make_string(CURRY_VERSION);
}

/* uname(2) is called once per query rather than cached: cheap syscall, and
 * caching would go stale across a container/VM migration or a kernel that
 * reports a changed hostname mid-process — not worth the complexity for
 * something this infrequently called. */
static curry_val fn_cpu_architecture(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    struct utsname u;
    if (uname(&u) < 0) return curry_make_bool(false);
    return curry_make_string(u.machine);
}

static curry_val fn_os_name(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    struct utsname u;
    if (uname(&u) < 0) return curry_make_bool(false);
    return curry_make_string(u.sysname);
}

static curry_val fn_os_version(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    struct utsname u;
    if (uname(&u) < 0) return curry_make_bool(false);
    return curry_make_string(u.release);
}

static curry_val fn_machine_name(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    char buf[256];
    if (gethostname(buf, sizeof(buf)) < 0) return curry_make_bool(false);
    buf[sizeof(buf) - 1] = '\0';
    return curry_make_string(buf);
}

/* ---- registration ----------------------------------------------------------- */

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "file-info",            fn_file_info,            1, 2, NULL);
    curry_define_fn(vm, "file-info?",            fn_file_info_p,         1, 1, NULL);
    curry_define_fn(vm, "file-info:device",      fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_DEVICE);
    curry_define_fn(vm, "file-info:inode",       fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_INODE);
    curry_define_fn(vm, "file-info:mode",        fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_MODE);
    curry_define_fn(vm, "file-info:nlinks",      fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_NLINKS);
    curry_define_fn(vm, "file-info:uid",         fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_UID);
    curry_define_fn(vm, "file-info:gid",         fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_GID);
    curry_define_fn(vm, "file-info:rdev",        fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_RDEV);
    curry_define_fn(vm, "file-info:size",        fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_SIZE);
    curry_define_fn(vm, "file-info:blksize",     fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_BLKSIZE);
    curry_define_fn(vm, "file-info:blocks",      fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_BLOCKS);
    curry_define_fn(vm, "file-info:atime",       fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_ATIME);
    curry_define_fn(vm, "file-info:mtime",       fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_MTIME);
    curry_define_fn(vm, "file-info:ctime",       fn_file_info_ref, 1, 1, (void *)(intptr_t)FI_CTIME);

    curry_define_fn(vm, "file-info-directory?",  fn_file_info_type_p, 1, 1, (void *)(intptr_t)S_IFDIR);
    curry_define_fn(vm, "file-info-regular?",    fn_file_info_type_p, 1, 1, (void *)(intptr_t)S_IFREG);
    curry_define_fn(vm, "file-info-symlink?",    fn_file_info_type_p, 1, 1, (void *)(intptr_t)S_IFLNK);
    curry_define_fn(vm, "file-info-fifo?",       fn_file_info_type_p, 1, 1, (void *)(intptr_t)S_IFIFO);
    curry_define_fn(vm, "file-info-socket?",     fn_file_info_type_p, 1, 1, (void *)(intptr_t)S_IFSOCK);
    curry_define_fn(vm, "file-info-device?",     fn_file_info_type_p, 1, 1, (void *)(intptr_t)S_IFBLK);
    curry_define_fn(vm, "file-info-char-device?",fn_file_info_type_p, 1, 1, (void *)(intptr_t)S_IFCHR);

    curry_define_fn(vm, "create-directory",      fn_create_directory,    1, 2, NULL);
    curry_define_fn(vm, "delete-directory",      fn_delete_directory,    1, 1, NULL);
    curry_define_fn(vm, "directory-files",       fn_directory_files,     1, 2, NULL);
    curry_define_fn(vm, "open-directory",        fn_open_directory,      1, 1, NULL);
    curry_define_fn(vm, "read-directory",        fn_read_directory,      1, 1, NULL);
    curry_define_fn(vm, "close-directory",       fn_close_directory,     1, 1, NULL);

    curry_define_fn(vm, "rename-file",           fn_rename_file,         2, 2, NULL);
    curry_define_fn(vm, "create-hard-link",      fn_create_hard_link,    2, 2, NULL);
    curry_define_fn(vm, "create-symlink",        fn_create_symlink,      2, 2, NULL);
    curry_define_fn(vm, "read-symlink",          fn_read_symlink,        1, 1, NULL);
    curry_define_fn(vm, "real-path",             fn_real_path,           1, 1, NULL);
    curry_define_fn(vm, "truncate-file",         fn_truncate_file,       2, 2, NULL);
    curry_define_fn(vm, "set-file-mode",         fn_set_file_mode,       2, 2, NULL);
    curry_define_fn(vm, "set-file-owner",        fn_set_file_owner,      2, 3, NULL);
    curry_define_fn(vm, "set-file-times",        fn_set_file_times,      1, 3, NULL);

    curry_define_fn(vm, "umask",                 fn_umask,               0, 0, NULL);
    curry_define_fn(vm, "set-umask!",            fn_set_umask,           1, 1, NULL);
    curry_define_fn(vm, "current-directory",     fn_current_directory,   0, 0, NULL);
    curry_define_fn(vm, "set-current-directory!",fn_set_current_directory,1,1, NULL);
    curry_define_fn(vm, "pid",                   fn_pid,                 0, 0, NULL);
    curry_define_fn(vm, "nice",                  fn_nice,                0, 1, NULL);

    curry_define_fn(vm, "user-uid",              fn_user_uid,            0, 0, NULL);
    curry_define_fn(vm, "user-gid",              fn_user_gid,            0, 0, NULL);
    curry_define_fn(vm, "user-effective-uid",    fn_user_effective_uid,  0, 0, NULL);
    curry_define_fn(vm, "user-effective-gid",    fn_user_effective_gid,  0, 0, NULL);
    curry_define_fn(vm, "user-supplementary-gids",fn_user_supplementary_gids,0,0,NULL);

    curry_define_fn(vm, "user-info",             fn_user_info,           1, 1, NULL);
    curry_define_fn(vm, "user-info?",            fn_user_info_p,         1, 1, NULL);
    curry_define_fn(vm, "user-info:name",        fn_user_info_ref, 1, 1, (void *)(intptr_t)UI_NAME);
    curry_define_fn(vm, "user-info:uid",         fn_user_info_ref, 1, 1, (void *)(intptr_t)UI_UID);
    curry_define_fn(vm, "user-info:gid",         fn_user_info_ref, 1, 1, (void *)(intptr_t)UI_GID);
    curry_define_fn(vm, "user-info:home-dir",    fn_user_info_ref, 1, 1, (void *)(intptr_t)UI_HOME);
    curry_define_fn(vm, "user-info:shell",       fn_user_info_ref, 1, 1, (void *)(intptr_t)UI_SHELL);
    curry_define_fn(vm, "user-info:full-name",   fn_user_info_ref, 1, 1, (void *)(intptr_t)UI_FULLNAME);

    curry_define_fn(vm, "group-info",            fn_group_info,          1, 1, NULL);
    curry_define_fn(vm, "group-info?",           fn_group_info_p,        1, 1, NULL);
    curry_define_fn(vm, "group-info:name",       fn_group_info_ref, 1, 1, (void *)(intptr_t)GI_NAME);
    curry_define_fn(vm, "group-info:gid",        fn_group_info_ref, 1, 1, (void *)(intptr_t)GI_GID);

    curry_define_fn(vm, "posix-time",            fn_posix_time,          0, 0, NULL);
    curry_define_fn(vm, "monotonic-time",        fn_monotonic_time,      0, 0, NULL);

    curry_define_fn(vm, "set-environment-variable!",    fn_set_environment_variable,    2, 2, NULL);
    curry_define_fn(vm, "delete-environment-variable!", fn_delete_environment_variable, 1, 1, NULL);

    curry_define_fn(vm, "terminal?",             fn_terminal_p,          0, 1, NULL);

    curry_define_fn(vm, "implementation-name",    fn_implementation_name,    0, 0, NULL);
    curry_define_fn(vm, "implementation-version", fn_implementation_version, 0, 0, NULL);
    curry_define_fn(vm, "cpu-architecture",       fn_cpu_architecture,       0, 0, NULL);
    curry_define_fn(vm, "machine-name",           fn_machine_name,           0, 0, NULL);
    curry_define_fn(vm, "os-name",                fn_os_name,                0, 0, NULL);
    curry_define_fn(vm, "os-version",              fn_os_version,             0, 0, NULL);
}
