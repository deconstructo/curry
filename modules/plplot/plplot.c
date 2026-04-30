/* PLplot module for Curry Scheme.
 * Ported from siod-tr/siod_plplot.c (SIOD-TR project).
 * Requires: libplplot-dev  (sudo apt install libplplot-dev)
 */

#include <curry.h>
#include <plplot/plplot.h>
#include <stdlib.h>
#include <string.h>

/* ---- Array conversion helpers ---- */

static PLFLT *list_to_plflt(curry_val lst, int *out_n) {
    int n = 0;
    for (curry_val p = lst; curry_is_pair(p); p = curry_cdr(p)) n++;
    *out_n = n;
    if (n == 0) return NULL;
    PLFLT *arr = malloc((size_t)n * sizeof(PLFLT));
    int i = 0;
    for (curry_val p = lst; curry_is_pair(p); p = curry_cdr(p), i++) {
        curry_val v = curry_car(p);
        arr[i] = curry_is_fixnum(v) ? (PLFLT)curry_fixnum(v)
                                    : (PLFLT)curry_float(v);
    }
    return arr;
}

static PLFLT **list_to_plflt2d(curry_val lst, int *out_nx, int *out_ny) {
    int ny = 0;
    for (curry_val p = lst; curry_is_pair(p); p = curry_cdr(p)) ny++;
    if (ny == 0) { *out_nx = *out_ny = 0; return NULL; }
    int nx = 0;
    for (curry_val p = curry_car(lst); curry_is_pair(p); p = curry_cdr(p)) nx++;
    PLFLT **arr = malloc((size_t)ny * sizeof(PLFLT *));
    int i = 0;
    for (curry_val row = lst; curry_is_pair(row); row = curry_cdr(row), i++) {
        arr[i] = malloc((size_t)nx * sizeof(PLFLT));
        int j = 0;
        for (curry_val p = curry_car(row); curry_is_pair(p); p = curry_cdr(p), j++) {
            curry_val v = curry_car(p);
            arr[i][j] = curry_is_fixnum(v) ? (PLFLT)curry_fixnum(v)
                                           : (PLFLT)curry_float(v);
        }
    }
    *out_nx = nx; *out_ny = ny;
    return arr;
}

static void free2d(PLFLT **arr, int ny) {
    if (!arr) return;
    for (int i = 0; i < ny; i++) free(arr[i]);
    free(arr);
}

static double to_double(curry_val v, const char *ctx) {
    if (curry_is_fixnum(v)) return (double)curry_fixnum(v);
    if (curry_is_float(v))  return curry_float(v);
    curry_error("%s: expected number", ctx);
}

/* ---- Setup / teardown ---- */

static curry_val fn_plot_init(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud; plinit(); return curry_void();
}
static curry_val fn_plot_end(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud; plend(); return curry_void();
}
static curry_val fn_plot_device(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[0])) curry_error("plot-device: expected string");
    plsdev(curry_string(av[0])); return curry_void();
}
static curry_val fn_plot_output(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[0])) curry_error("plot-output: expected string");
    plsfnam(curry_string(av[0])); return curry_void();
}
static curry_val fn_plot_font_size(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud; plschr(0.0, (PLFLT)to_double(av[0], "plot-font-size")); return curry_void();
}

/* ---- Environment ---- */

static curry_val fn_plot_env(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    plenv((PLFLT)to_double(av[0],"plot-env"), (PLFLT)to_double(av[1],"plot-env"),
          (PLFLT)to_double(av[2],"plot-env"), (PLFLT)to_double(av[3],"plot-env"), 0, 0);
    return curry_void();
}
static curry_val fn_plot_env_log(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    plenv((PLFLT)to_double(av[0],"plot-env-log"), (PLFLT)to_double(av[1],"plot-env-log"),
          (PLFLT)to_double(av[2],"plot-env-log"), (PLFLT)to_double(av[3],"plot-env-log"),
          0, (PLINT)to_double(av[4],"plot-env-log"));
    return curry_void();
}
static curry_val fn_plot_labels(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[0]) || !curry_is_string(av[1]) || !curry_is_string(av[2]))
        curry_error("plot-labels: expected 3 strings");
    pllab(curry_string(av[0]), curry_string(av[1]), curry_string(av[2]));
    return curry_void();
}
static curry_val fn_plot_box(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[0]) || !curry_is_string(av[1]))
        curry_error("plot-box: expected 2 strings");
    plbox(curry_string(av[0]), 0.0, 0, curry_string(av[1]), 0.0, 0);
    return curry_void();
}

/* ---- Color / style ---- */

static curry_val fn_plot_color(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud; plcol0((PLINT)to_double(av[0],"plot-color")); return curry_void();
}
static curry_val fn_plot_color_rgb(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    plscol0(15, (PLINT)to_double(av[0],"plot-color-rgb"),
                (PLINT)to_double(av[1],"plot-color-rgb"),
                (PLINT)to_double(av[2],"plot-color-rgb"));
    plcol0(15); return curry_void();
}
static curry_val fn_plot_width(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud; plwidth((PLFLT)to_double(av[0],"plot-width")); return curry_void();
}
static curry_val fn_plot_bg_color(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    plscolbg((PLINT)to_double(av[0],"plot-background-color"),
             (PLINT)to_double(av[1],"plot-background-color"),
             (PLINT)to_double(av[2],"plot-background-color"));
    return curry_void();
}

/* ---- 2D plotting ---- */

static curry_val fn_plot_line(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int nx, ny;
    PLFLT *x = list_to_plflt(av[0], &nx);
    PLFLT *y = list_to_plflt(av[1], &ny);
    if (!x || !y || nx != ny) { free(x); free(y); curry_error("plot-line: x and y must be equal-length number lists"); }
    plline(nx, x, y); free(x); free(y); return curry_void();
}
static curry_val fn_plot_points(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int nx, ny;
    PLFLT *x = list_to_plflt(av[0], &nx);
    PLFLT *y = list_to_plflt(av[1], &ny);
    PLINT sym = (PLINT)to_double(av[2], "plot-points");
    if (!x || !y || nx != ny) { free(x); free(y); curry_error("plot-points: length mismatch"); }
    plpoin(nx, x, y, sym); free(x); free(y); return curry_void();
}
static curry_val fn_plot_histogram(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int n;
    PLFLT *data = list_to_plflt(av[0], &n);
    if (!data) curry_error("plot-histogram: empty data");
    plhist(n, data, (PLFLT)to_double(av[1],"plot-histogram"), (PLFLT)to_double(av[2],"plot-histogram"),
           (PLINT)to_double(av[3],"plot-histogram"), 0);
    free(data); return curry_void();
}
static curry_val fn_plot_error_y(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int nx, nymin, nymax;
    PLFLT *x = list_to_plflt(av[0], &nx), *ymin = list_to_plflt(av[1], &nymin), *ymax = list_to_plflt(av[2], &nymax);
    if (nx != nymin || nx != nymax) { free(x); free(ymin); free(ymax); curry_error("plot-error-y: length mismatch"); }
    plerry(nx, x, ymin, ymax); free(x); free(ymin); free(ymax); return curry_void();
}
static curry_val fn_plot_error_x(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int nxmin, nxmax, ny;
    PLFLT *xmin = list_to_plflt(av[0], &nxmin), *xmax = list_to_plflt(av[1], &nxmax), *y = list_to_plflt(av[2], &ny);
    if (nxmin != nxmax || nxmin != ny) { free(xmin); free(xmax); free(y); curry_error("plot-error-x: length mismatch"); }
    plerrx(nxmin, xmin, xmax, y); free(xmin); free(xmax); free(y); return curry_void();
}

/* ---- 3D plotting ---- */

static curry_val fn_plot_3d_init(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    PLFLT x1=(PLFLT)to_double(av[0],"plot-3d-init"), x2=(PLFLT)to_double(av[1],"plot-3d-init");
    PLFLT y1=(PLFLT)to_double(av[2],"plot-3d-init"), y2=(PLFLT)to_double(av[3],"plot-3d-init");
    PLFLT z1=(PLFLT)to_double(av[4],"plot-3d-init"), z2=(PLFLT)to_double(av[5],"plot-3d-init");
    PLFLT alt=(PLFLT)to_double(av[6],"plot-3d-init"), az=(PLFLT)to_double(av[7],"plot-3d-init");
    pladv(0); plvpor(0.0,1.0,0.0,0.9); plwind(-1.0,1.0,-0.9,1.1);
    plw3d(1.0,1.0,1.0,x1,x2,y1,y2,z1,z2,alt,az); return curry_void();
}
static curry_val fn_plot_3d_box(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[0]) || !curry_is_string(av[1]) || !curry_is_string(av[2]))
        curry_error("plot-3d-box: expected 3 strings");
    plbox3("bnstu",curry_string(av[0]),0.0,0, "bnstu",curry_string(av[1]),0.0,0,
           "bcdmnstuv",curry_string(av[2]),0.0,0); return curry_void();
}
static curry_val fn_plot_3d_line(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int nx, ny, nz;
    PLFLT *x=list_to_plflt(av[0],&nx), *y=list_to_plflt(av[1],&ny), *z=list_to_plflt(av[2],&nz);
    if (nx!=ny||nx!=nz){free(x);free(y);free(z);curry_error("plot-3d-line: length mismatch");}
    plline3(nx,x,y,z); free(x); free(y); free(z); return curry_void();
}
static curry_val fn_plot_3d_surface(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int nx,ny,nzc,nzr;
    PLFLT *x=list_to_plflt(av[0],&nx), *y=list_to_plflt(av[1],&ny);
    PLFLT **z=list_to_plflt2d(av[2],&nzc,&nzr);
    if(!x||!y||!z||nx!=nzc||ny!=nzr){free(x);free(y);free2d(z,nzr);curry_error("plot-3d-surface: dimension mismatch");}
    plsurf3d(x,y,(const PLFLT*const*)z,nx,ny,0,NULL,0); free(x);free(y);free2d(z,nzr); return curry_void();
}
static curry_val fn_plot_3d_mesh(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int nx,ny,nzc,nzr;
    PLFLT *x=list_to_plflt(av[0],&nx), *y=list_to_plflt(av[1],&ny);
    PLFLT **z=list_to_plflt2d(av[2],&nzc,&nzr);
    if(!x||!y||!z||nx!=nzc||ny!=nzr){free(x);free(y);free2d(z,nzr);curry_error("plot-3d-mesh: dimension mismatch");}
    plmesh(x,y,(const PLFLT*const*)z,nx,ny,3); free(x);free(y);free2d(z,nzr); return curry_void();
}

/* ---- Layout ---- */

static curry_val fn_plot_subplot(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    plssub((PLINT)to_double(av[0],"plot-subplot"), (PLINT)to_double(av[1],"plot-subplot")); return curry_void();
}
static curry_val fn_plot_advance(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud; pladv(0); return curry_void();
}

/* ---- Annotations ---- */

static curry_val fn_plot_text(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[2])) curry_error("plot-text: expected string");
    plptex((PLFLT)to_double(av[0],"plot-text"), (PLFLT)to_double(av[1],"plot-text"),
           1.0, 0.0, 0.5, curry_string(av[2])); return curry_void();
}
static curry_val fn_plot_mtex(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[0]) || !curry_is_string(av[4]))
        curry_error("plot-mtex: expected strings for side and text");
    plmtex(curry_string(av[0]), (PLFLT)to_double(av[1],"plot-mtex"),
           (PLFLT)to_double(av[2],"plot-mtex"), (PLFLT)to_double(av[3],"plot-mtex"),
           curry_string(av[4])); return curry_void();
}

/* ---- Utilities ---- */

static curry_val fn_plot_clear(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud; plclear(); return curry_void();
}
static curry_val fn_plot_flush(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud; plflush(); return curry_void();
}
static curry_val fn_plot_version(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    char ver[80]; plgver(ver); return curry_make_string(ver);
}
static curry_val fn_plot_page_dimensions(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    PLFLT xp, yp; PLINT xleng, yleng, xoff, yoff;
    plgpage(&xp, &yp, &xleng, &yleng, &xoff, &yoff);
    return curry_make_pair(curry_make_fixnum(xleng), curry_make_fixnum(yleng));
}

/* ---- Module init ---- */

void curry_module_init(CurryVM *vm) {
#define DEF(name, fn, mn, mx) curry_define_fn(vm, name, fn, mn, mx, NULL)
    DEF("plot-init",              fn_plot_init,           0, 0);
    DEF("plot-end",               fn_plot_end,            0, 0);
    DEF("plot-device",            fn_plot_device,         1, 1);
    DEF("plot-output",            fn_plot_output,         1, 1);
    DEF("plot-font-size",         fn_plot_font_size,      1, 1);
    DEF("plot-env",               fn_plot_env,            4, 4);
    DEF("plot-env-log",           fn_plot_env_log,        5, 5);
    DEF("plot-labels",            fn_plot_labels,         3, 3);
    DEF("plot-box",               fn_plot_box,            2, 2);
    DEF("plot-color",             fn_plot_color,          1, 1);
    DEF("plot-color-rgb",         fn_plot_color_rgb,      3, 3);
    DEF("plot-width",             fn_plot_width,          1, 1);
    DEF("plot-background-color",  fn_plot_bg_color,       3, 3);
    DEF("plot-line",              fn_plot_line,           2, 2);
    DEF("plot-points",            fn_plot_points,         3, 3);
    DEF("plot-histogram",         fn_plot_histogram,      4, 4);
    DEF("plot-error-y",           fn_plot_error_y,        3, 3);
    DEF("plot-error-x",           fn_plot_error_x,        3, 3);
    DEF("plot-3d-init",           fn_plot_3d_init,        8, 8);
    DEF("plot-3d-box",            fn_plot_3d_box,         3, 3);
    DEF("plot-3d-line",           fn_plot_3d_line,        3, 3);
    DEF("plot-3d-surface",        fn_plot_3d_surface,     3, 3);
    DEF("plot-3d-mesh",           fn_plot_3d_mesh,        3, 3);
    DEF("plot-subplot",           fn_plot_subplot,        2, 2);
    DEF("plot-advance",           fn_plot_advance,        0, 0);
    DEF("plot-text",              fn_plot_text,           3, 3);
    DEF("plot-mtex",              fn_plot_mtex,           5, 5);
    DEF("plot-clear",             fn_plot_clear,          0, 0);
    DEF("plot-flush",             fn_plot_flush,          0, 0);
    DEF("plot-version",           fn_plot_version,        0, 0);
    DEF("plot-page-dimensions",   fn_plot_page_dimensions, 0, 0);
#undef DEF
}
