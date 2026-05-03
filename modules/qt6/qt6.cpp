/*
 * modules/qt6/qt6.cpp — Qt6 UI + graphics module for Curry Scheme
 *
 * Three API layers:
 *
 *   Layer 1  qt-*   Raw Qt6 primitives: device dimensions, GPU query,
 *                   event processing.
 *
 *   Layer 2  gfx-*  GPU-accelerated 2D graphics via QPainter on
 *                   QOpenGLWidget.  Primitives: clear, color, pen,
 *                   shapes, text, transforms, batch drawing.
 *                   CPU software rendering falls back automatically
 *                   via Mesa/llvmpipe when no GPU is present.
 *
 *   Layer 3  (natural names)  Full UI framework: windows, menus,
 *                   toolbars, status bars, canvas, sliders, buttons,
 *                   checkboxes, radio groups, dropdowns, spin boxes,
 *                   text inputs, progress bars, timers.
 *
 * Callbacks: every event/widget takes a plain Scheme lambda.
 *   Draw    : (lambda (painter w h) ...)
 *   Key     : (lambda (key mods) ...)   key=string, mods=list of symbols
 *   Mouse   : (lambda (event button x y mods) ...)  event='press|'release|'move
 *   Timer   : (lambda () ...)
 *   Button  : (lambda () ...)
 *   Toggle  : (lambda (on?) ...)        on?=#t/#f
 *   Dropdown: (lambda (index) ...)
 *   Slider  : (lambda (value) ...)      optional — value is also readable
 *   Radio   : (lambda (index) ...)
 *   Spin    : (lambda (value) ...)
 *   Text    : (lambda (string) ...)
 *
 * Animation pattern:
 *   (define t (make-timer 16 (lambda () (step!) (canvas-redraw! canvas))))
 *   (timer-start! t)
 *
 * Cross-platform: Linux X11/Wayland, macOS (Metal via Qt), Windows (D3D/ANGLE).
 */

#include <curry.h>
#include "eval.h"   /* SCM_PROTECT — catches longjmp-based Scheme exceptions */

/* Wrap every curry_apply at a C++→Scheme boundary.  Any Scheme exception
 * (longjmp) must not escape into Qt's C++ event machinery, as Qt does not
 * handle foreign non-local exits and the result is undefined behaviour. */
#define SCHEME_CALL(call) do { ExnHandler _sch_h_; \
    SCM_PROTECT(_sch_h_, (call), (void)0); } while(0)

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QGroupBox>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QProgressBar>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPolygonF>
#include <QImage>
#include <QTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QShowEvent>
#include <QKeySequence>
#include <QSizePolicy>
#include <QVariant>
#include <QString>
#include <QByteArray>

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

/* =========================================================================
 * GC protection for Scheme procedure values captured in Qt callbacks.
 *
 * Boehm GC conservatively scans the data segment of every loaded .so
 * (it reads /proc/self/maps and marks all writable non-heap segments as
 * roots).  It does NOT scan memory allocated by the system malloc that Qt
 * uses internally for signal-slot connection storage.
 *
 * To keep Scheme procedure values alive despite being referenced only from
 * Qt connection objects, we store each one in a Scheme pair list rooted at
 * s_proc_roots.  s_proc_roots lives in this .so's data segment, so it is
 * always a conservative root; the list itself is on the GC heap and is
 * transitively scanned.
 *
 * Note: procs are never removed (the list grows monotonically).  This is
 * acceptable because Qt callbacks live for the duration of the program.
 * ========================================================================= */

static curry_val s_proc_roots = 0;  /* 0 until module_init sets it to nil */

static void keep_alive(curry_val v) {
    s_proc_roots = curry_make_pair(v, s_proc_roots);
}

/* =========================================================================
 * QApplication singleton
 * ========================================================================= */

static int    s_argc    = 1;
static char   s_argv0[] = "curry";
static char  *s_argv[]  = { s_argv0, nullptr };
static QApplication *s_app = nullptr;

static void ensure_app() {
    if (s_app) return;
    QSurfaceFormat fmt;
    fmt.setSamples(4);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);
    s_app = new QApplication(s_argc, s_argv);
    s_app->setApplicationName("Curry");
}

/* =========================================================================
 * Tagged handle system
 * Each opaque Qt object is wrapped as a Scheme pair (tag-symbol . bytevector)
 * where the bytevector stores the raw pointer bytes.
 * ========================================================================= */

static curry_val ptr_to_val(const char *tag, void *ptr) {
    curry_val bv = curry_make_bytevector((uint32_t)sizeof(void *), 0);
    for (size_t i = 0; i < sizeof(void *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&ptr)[i]);
    return curry_make_pair(curry_make_symbol(tag), bv);
}

static void *val_to_ptr(const char *tag, curry_val v) {
    if (!curry_is_pair(v) || !curry_is_symbol(curry_car(v)))
        curry_error("qt6: not a handle (expected %s)", tag);
    const char *got = curry_symbol(curry_car(v));
    if (strcmp(got, tag) != 0)
        curry_error("qt6: wrong handle type — expected %s, got %s", tag, got);
    curry_val bv = curry_cdr(v);
    void *ptr = nullptr;
    for (size_t i = 0; i < sizeof(void *); i++)
        ((uint8_t *)&ptr)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return ptr;
}

/* =========================================================================
 * Structs
 * ========================================================================= */

struct WinState;
struct SliderState;

/* Per-window state.  Owned by the CurryWindow/CurryCanvas pair. */
struct WinState {
    QMainWindow   *win;
    QOpenGLWidget *canvas;
    QVBoxLayout   *sidebar_layout;
    QPainter      *live_painter;   /* non-null only inside paintEvent */
    curry_val      draw_proc;
    curry_val      key_proc;
    curry_val      mouse_proc;
    curry_val      realize_proc;
    curry_val      close_proc;
    int            cur_w, cur_h;
    bool           realized;
    bool           gpu_ok;
};

/* Per-slider state — stores mapping from integer tick → float value. */
struct SliderState {
    QSlider *slider;
    double   lo, hi;
    int      steps;
    double value() const {
        if (steps == 0) return lo;
        return lo + (hi - lo) * (double)slider->value() / (double)steps;
    }
};

/* =========================================================================
 * CurryCanvas — QOpenGLWidget subclass
 * No Q_OBJECT: we only override virtuals, no new signals/slots.
 * Lambda-based QObject::connect() works fine on the Qt side.
 * ========================================================================= */

class CurryCanvas : public QOpenGLWidget {
public:
    WinState *ws;
    explicit CurryCanvas(WinState *ws_, QWidget *parent = nullptr)
        : QOpenGLWidget(parent), ws(ws_) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }
protected:
    void initializeGL() override { ws->gpu_ok = true; }
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override   { fire_mouse("press",   ev); }
    void mouseReleaseEvent(QMouseEvent *ev) override { fire_mouse("release", ev); }
    void mouseMoveEvent(QMouseEvent *ev) override    { fire_mouse("move",    ev); }
    void resizeEvent(QResizeEvent *ev) override {
        QOpenGLWidget::resizeEvent(ev);
        ws->cur_w = width();
        ws->cur_h = height();
    }
private:
    void fire_mouse(const char *event_type, QMouseEvent *ev);
};

/* =========================================================================
 * CurryWindow — QMainWindow subclass
 * ========================================================================= */

class CurryWindow : public QMainWindow {
public:
    WinState *ws;
    explicit CurryWindow(WinState *ws_) : QMainWindow(nullptr), ws(ws_) {
        setAttribute(Qt::WA_DeleteOnClose, false);
    }
protected:
    void showEvent(QShowEvent *ev) override {
        QMainWindow::showEvent(ev);
        if (!ws->realized) {
            ws->realized = true;
            if (!curry_is_bool(ws->realize_proc))
                SCHEME_CALL(curry_apply(ws->realize_proc, 0, nullptr));
        }
    }
    void closeEvent(QCloseEvent *ev) override {
        if (!curry_is_bool(ws->close_proc))
            SCHEME_CALL(curry_apply(ws->close_proc, 0, nullptr));
        ev->ignore();  /* Scheme decides whether to quit */
    }
    void keyPressEvent(QKeyEvent *ev) override {
        /* Forward to canvas via the public event dispatcher */
        QApplication::sendEvent(ws->canvas, ev);
    }
};

/* =========================================================================
 * CurryCanvas implementations
 * ========================================================================= */

static const char *qt_key_str(int key, const QString &text) {
    static char buf[64];
    switch (key) {
    case Qt::Key_Escape:    return "Escape";
    case Qt::Key_Return:    return "Return";
    case Qt::Key_Enter:     return "KP_Enter";
    case Qt::Key_Space:     return "space";
    case Qt::Key_Tab:       return "Tab";
    case Qt::Key_Backspace: return "BackSpace";
    case Qt::Key_Delete:    return "Delete";
    case Qt::Key_Insert:    return "Insert";
    case Qt::Key_Left:      return "Left";
    case Qt::Key_Right:     return "Right";
    case Qt::Key_Up:        return "Up";
    case Qt::Key_Down:      return "Down";
    case Qt::Key_Home:      return "Home";
    case Qt::Key_End:       return "End";
    case Qt::Key_PageUp:    return "Page_Up";
    case Qt::Key_PageDown:  return "Page_Down";
    case Qt::Key_F1:  return "F1";  case Qt::Key_F2:  return "F2";
    case Qt::Key_F3:  return "F3";  case Qt::Key_F4:  return "F4";
    case Qt::Key_F5:  return "F5";  case Qt::Key_F6:  return "F6";
    case Qt::Key_F7:  return "F7";  case Qt::Key_F8:  return "F8";
    case Qt::Key_F9:  return "F9";  case Qt::Key_F10: return "F10";
    case Qt::Key_F11: return "F11"; case Qt::Key_F12: return "F12";
    default: break;
    }
    if (!text.isEmpty()) {
        QByteArray ba = text.toUtf8();
        strncpy(buf, ba.constData(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }
    QByteArray ba = QKeySequence(key).toString().toUtf8();
    strncpy(buf, ba.constData(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

static curry_val build_mods(Qt::KeyboardModifiers m) {
    curry_val mods = curry_nil();
    if (m & Qt::MetaModifier)    mods = curry_make_pair(curry_make_symbol("meta"),  mods);
    if (m & Qt::AltModifier)     mods = curry_make_pair(curry_make_symbol("alt"),   mods);
    if (m & Qt::ControlModifier) mods = curry_make_pair(curry_make_symbol("ctrl"),  mods);
    if (m & Qt::ShiftModifier)   mods = curry_make_pair(curry_make_symbol("shift"), mods);
    return mods;
}

void CurryCanvas::keyPressEvent(QKeyEvent *ev) {
    if (curry_is_bool(ws->key_proc)) return;
    curry_val argv[2] = {
        curry_make_string(qt_key_str(ev->key(), ev->text())),
        build_mods(ev->modifiers())
    };
    SCHEME_CALL(curry_apply(ws->key_proc, 2, argv));
}

void CurryCanvas::fire_mouse(const char *etype, QMouseEvent *ev) {
    if (curry_is_bool(ws->mouse_proc)) return;
    const char *btn = "none";
    switch (ev->button()) {
    case Qt::LeftButton:   btn = "left";   break;
    case Qt::RightButton:  btn = "right";  break;
    case Qt::MiddleButton: btn = "middle"; break;
    default: break;
    }
    curry_val argv[5] = {
        curry_make_symbol(etype),
        curry_make_symbol(btn),
        curry_make_fixnum((intptr_t)ev->position().x()),
        curry_make_fixnum((intptr_t)ev->position().y()),
        build_mods(ev->modifiers())
    };
    SCHEME_CALL(curry_apply(ws->mouse_proc, 5, argv));
}

void CurryCanvas::paintEvent(QPaintEvent *) {
    if (curry_is_bool(ws->draw_proc)) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    ws->live_painter = &painter;
    ws->cur_w = width();
    ws->cur_h = height();
    curry_val argv[3] = {
        ptr_to_val("qt6-painter", &painter),
        curry_make_fixnum(ws->cur_w),
        curry_make_fixnum(ws->cur_h)
    };
    /* Wrap curry_apply with SCM_PROTECT so a Scheme exception (longjmp) does
     * not escape past this stack frame.  Without this, any error thrown from
     * the draw callback would bypass the QPainter destructor and leave the
     * QOpenGLWidget in a permanently-painting state, crashing the next frame. */
    ExnHandler h;
    SCM_PROTECT(h,
        curry_apply(ws->draw_proc, 3, argv),
        /* on Scheme error: swallow, finish the paint event normally */
        (void)0);
    ws->live_painter = nullptr;
}

/* =========================================================================
 * Handle macros
 * ========================================================================= */

#define win_to_val(ws)      ptr_to_val("qt6-window",  (ws))
#define canvas_to_val(ws)   ptr_to_val("qt6-canvas",  (ws))
#define layout_to_val(l)    ptr_to_val("qt6-box",     (l))
#define widget_to_val(w)    ptr_to_val("qt6-widget",  (w))
#define menubar_to_val(m)   ptr_to_val("qt6-menubar", (m))
#define menu_to_val(m)      ptr_to_val("qt6-menu",    (m))
#define timer_to_val(t)     ptr_to_val("qt6-timer",   (t))

#define val_to_win(v)       ((WinState *)   val_to_ptr("qt6-window",  (v)))
#define val_to_canvas(v)    ((WinState *)   val_to_ptr("qt6-canvas",  (v)))
#define val_to_layout(v)    ((QVBoxLayout *)val_to_ptr("qt6-box",     (v)))
#define val_to_widget(v)    ((QWidget *)    val_to_ptr("qt6-widget",  (v)))
#define val_to_menubar(v)   ((QMenuBar *)   val_to_ptr("qt6-menubar", (v)))
#define val_to_menu(v)      ((QMenu *)      val_to_ptr("qt6-menu",    (v)))
#define val_to_timer(v)     ((QTimer *)     val_to_ptr("qt6-timer",   (v)))

static QPainter *val_to_painter(curry_val v) {
    return (QPainter *)val_to_ptr("qt6-painter", v);
}

/* =========================================================================
 * Layer 3 — Window
 * ========================================================================= */

static curry_val fn_make_window(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    ensure_app();
    const char *title = curry_string(av[0]);
    int w = (int)curry_float(av[1]);
    int h = (int)curry_float(av[2]);

    auto *ws           = new WinState();
    ws->draw_proc      = curry_make_bool(false);
    ws->key_proc       = curry_make_bool(false);
    ws->mouse_proc     = curry_make_bool(false);
    ws->realize_proc   = curry_make_bool(false);
    ws->close_proc     = curry_make_bool(false);
    ws->live_painter   = nullptr;
    ws->cur_w = w; ws->cur_h = h;
    ws->realized = false;
    ws->gpu_ok   = false;

    auto *cwin = new CurryWindow(ws);
    cwin->setWindowTitle(QString::fromUtf8(title));
    cwin->resize(w, h);

    /* Central area: scrollable sidebar (left, 210 px) + canvas (expanding) */
    auto *central = new QWidget(cwin);
    auto *hbox    = new QHBoxLayout(central);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(0);

    auto *scroll = new QScrollArea();
    scroll->setFixedWidth(210);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *sbw = new QWidget();
    auto *sbl = new QVBoxLayout(sbw);
    sbl->setAlignment(Qt::AlignTop);
    sbl->setContentsMargins(8, 8, 8, 8);
    sbl->setSpacing(6);
    scroll->setWidget(sbw);

    auto *canvas = new CurryCanvas(ws, central);
    hbox->addWidget(scroll);
    hbox->addWidget(canvas, 1);
    cwin->setCentralWidget(central);

    ws->win            = cwin;
    ws->canvas         = canvas;
    ws->sidebar_layout = sbl;

    return win_to_val(ws);
}

static curry_val fn_window_on_close(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; keep_alive(av[1]); val_to_win(av[0])->close_proc = av[1]; return curry_void(); }
static curry_val fn_window_on_key(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; keep_alive(av[1]); val_to_win(av[0])->key_proc = av[1]; return curry_void(); }
static curry_val fn_window_on_realize(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; keep_alive(av[1]); val_to_win(av[0])->realize_proc = av[1]; return curry_void(); }

static curry_val fn_window_canvas(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; return canvas_to_val(val_to_win(av[0])); }
static curry_val fn_window_sidebar(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; return layout_to_val(val_to_win(av[0])->sidebar_layout); }

static curry_val fn_window_show(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; val_to_win(av[0])->win->show(); return curry_void(); }
static curry_val fn_window_hide(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; val_to_win(av[0])->win->hide(); return curry_void(); }
static curry_val fn_window_set_title(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    val_to_win(av[0])->win->setWindowTitle(QString::fromUtf8(curry_string(av[1])));
    return curry_void();
}

static curry_val fn_run_event_loop(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;(void)av; ensure_app(); s_app->exec(); return curry_void(); }
static curry_val fn_quit_event_loop(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;(void)av; if (s_app) s_app->quit(); return curry_void(); }

/* =========================================================================
 * Layer 3 — Canvas
 * ========================================================================= */

static curry_val fn_canvas_on_draw(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; keep_alive(av[1]); val_to_canvas(av[0])->draw_proc = av[1]; return curry_void(); }
static curry_val fn_canvas_on_mouse(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; keep_alive(av[1]); val_to_canvas(av[0])->mouse_proc = av[1]; return curry_void(); }
static curry_val fn_canvas_redraw(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; val_to_canvas(av[0])->canvas->update(); return curry_void(); }

/* =========================================================================
 * Layer 3 — Layout boxes
 * ========================================================================= */

static curry_val fn_box_add(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    val_to_layout(av[0])->addWidget(val_to_widget(av[1]));
    return curry_void();
}

/* Generic vertical/horizontal box containers — also usable as layout targets */
static curry_val fn_make_vbox(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;(void)av;
    auto *w = new QWidget();
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0,0,0,0); l->setSpacing(4); l->setAlignment(Qt::AlignTop);
    w->setProperty("_qt6_vl", QVariant((quintptr)(void*)l));
    return widget_to_val(w);
}
static curry_val fn_make_hbox(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;(void)av;
    auto *w = new QWidget();
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0,0,0,0); l->setSpacing(4);
    w->setProperty("_qt6_hl", QVariant((quintptr)(void*)l));
    return widget_to_val(w);
}
/* Add a child widget to a vbox or hbox widget */
static curry_val fn_layout_add(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QWidget *c = val_to_widget(av[0]);
    QWidget *child = val_to_widget(av[1]);
    QVariant vv = c->property("_qt6_vl");
    if (vv.isValid()) { ((QVBoxLayout *)vv.value<quintptr>())->addWidget(child); return curry_void(); }
    QVariant hv = c->property("_qt6_hl");
    if (hv.isValid()) { ((QHBoxLayout *)hv.value<quintptr>())->addWidget(child); return curry_void(); }
    curry_error("qt6: not a vbox or hbox widget");
}

/* Tabbed widget */
static curry_val fn_make_tabs(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;(void)av;
    return widget_to_val(new QTabWidget());
}
static curry_val fn_tabs_add(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    ((QTabWidget *)val_to_widget(av[0]))->addTab(val_to_widget(av[1]),
                                                   QString::fromUtf8(curry_string(av[2])));
    return curry_void();
}

/* Group box — titled frame containing a layout */
static curry_val fn_make_group_box(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    auto *gb = new QGroupBox(QString::fromUtf8(curry_string(av[0])));
    auto *l  = new QVBoxLayout(gb);
    l->setContentsMargins(8,8,8,8); l->setSpacing(6); l->setAlignment(Qt::AlignTop);
    gb->setProperty("_qt6_vl", QVariant((quintptr)(void*)l));
    return widget_to_val(gb);
}

/* =========================================================================
 * Layer 3 — Widgets
 * ========================================================================= */

static curry_val fn_make_label(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    auto *lbl = new QLabel(QString::fromUtf8(curry_string(av[0])));
    lbl->setWordWrap(true);
    lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return widget_to_val(lbl);
}

static curry_val fn_label_set_text(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    ((QLabel *)val_to_widget(av[0]))->setText(QString::fromUtf8(curry_string(av[1])));
    return curry_void();
}

static curry_val fn_make_button(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    auto *btn  = new QPushButton(QString::fromUtf8(curry_string(av[0])));
    curry_val proc = av[1];
    keep_alive(proc);
    QObject::connect(btn, &QPushButton::clicked, [proc]() {
        SCHEME_CALL(curry_apply(proc, 0, nullptr));
    });
    return widget_to_val(btn);
}

static curry_val fn_make_toggle(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    auto *cb   = new QCheckBox(QString::fromUtf8(curry_string(av[0])));
    cb->setChecked(curry_bool(av[1]));
    curry_val proc = av[2];
    keep_alive(proc);
    QObject::connect(cb, &QCheckBox::toggled, [proc](bool on) {
        curry_val argv[1] = { curry_make_bool(on) };
        SCHEME_CALL(curry_apply(proc, 1, argv));
    });
    return widget_to_val(cb);
}

/* (make-slider label lo hi step initial [callback])
 * callback is optional; slider value is also readable any time via slider-value */
static curry_val fn_make_slider(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *label   = curry_string(av[0]);
    double      lo      = curry_float(av[1]);
    double      hi      = curry_float(av[2]);
    double      step    = curry_float(av[3]);
    double      initial = curry_float(av[4]);

    int steps = (step > 0.0 && (hi - lo) / step < 1e6)
                ? (int)std::round((hi - lo) / step) : 10000;
    if (steps < 1) steps = 1;

    auto *ss    = new SliderState();
    ss->lo = lo; ss->hi = hi; ss->steps = steps;

    auto *container = new QWidget();
    auto *vbox      = new QVBoxLayout(container);
    vbox->setContentsMargins(0,0,0,0); vbox->setSpacing(2);

    auto *lbl_name = new QLabel(QString::fromUtf8(label));
    lbl_name->setStyleSheet("font-weight:bold;");
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, steps);
    int init_pos = (int)std::round((initial - lo) / (hi - lo) * steps);
    slider->setValue(std::max(0, std::min(steps, init_pos)));
    ss->slider = slider;

    auto *lbl_val = new QLabel(QString::number(ss->value(), 'f', 3));
    QObject::connect(slider, &QSlider::valueChanged, [lbl_val, ss](int) {
        lbl_val->setText(QString::number(ss->value(), 'f', 3));
    });

    /* Optional callback */
    if (ac >= 6 && !curry_is_bool(av[5])) {
        curry_val proc = av[5];
        keep_alive(proc);
        QObject::connect(slider, &QSlider::valueChanged, [proc, ss](int) {
            curry_val argv[1] = { curry_make_float(ss->value()) };
            SCHEME_CALL(curry_apply(proc, 1, argv));
        });
    }

    vbox->addWidget(lbl_name);
    vbox->addWidget(slider);
    vbox->addWidget(lbl_val);
    container->setProperty("_qt6_ss", QVariant((quintptr)(void*)ss));
    return widget_to_val(container);
}

static curry_val fn_slider_value(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QVariant v = val_to_widget(av[0])->property("_qt6_ss");
    if (!v.isValid()) curry_error("qt6: not a slider widget");
    return curry_make_float(((SliderState *)v.value<quintptr>())->value());
}

static curry_val fn_slider_set_value(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QVariant v = val_to_widget(av[0])->property("_qt6_ss");
    if (!v.isValid()) curry_error("qt6: not a slider widget");
    auto *ss = (SliderState *)v.value<quintptr>();
    double val = curry_float(av[1]);
    int pos = (int)std::round((val - ss->lo) / (ss->hi - ss->lo) * ss->steps);
    ss->slider->setValue(std::max(0, std::min(ss->steps, pos)));
    return curry_void();
}

/* (make-dropdown items initial-index callback) */
static curry_val fn_make_dropdown(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    auto *combo = new QComboBox();
    for (curry_val p = av[0]; !curry_is_nil(p); p = curry_cdr(p))
        combo->addItem(QString::fromUtf8(curry_string(curry_car(p))));
    int sel = (int)curry_float(av[1]);
    if (sel >= 0 && sel < combo->count()) combo->setCurrentIndex(sel);
    curry_val proc = av[2];
    keep_alive(proc);
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     [proc](int i) {
        curry_val argv[1] = { curry_make_fixnum(i) };
        SCHEME_CALL(curry_apply(proc, 1, argv));
    });
    return widget_to_val(combo);
}

static curry_val fn_dropdown_index(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    return curry_make_fixnum(((QComboBox *)val_to_widget(av[0]))->currentIndex());
}
static curry_val fn_dropdown_set_index(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    ((QComboBox *)val_to_widget(av[0]))->setCurrentIndex((int)curry_float(av[1]));
    return curry_void();
}
/* Alias for GTK-module compatibility */
static curry_val fn_dropdown_selected(int ac, curry_val *av, void *ud) {
    return fn_dropdown_index(ac, av, ud);
}

/* (make-radio-group items initial-index callback) */
static curry_val fn_make_radio_group(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    auto *container = new QWidget();
    auto *vbox      = new QVBoxLayout(container);
    vbox->setContentsMargins(0,0,0,0); vbox->setSpacing(4);
    auto *group = new QButtonGroup(container);
    int idx = 0, initial = (int)curry_float(av[1]);
    for (curry_val p = av[0]; !curry_is_nil(p); p = curry_cdr(p), idx++) {
        auto *rb = new QRadioButton(QString::fromUtf8(curry_string(curry_car(p))));
        group->addButton(rb, idx);
        vbox->addWidget(rb);
        if (idx == initial) rb->setChecked(true);
    }
    curry_val proc = av[2];
    keep_alive(proc);
    QObject::connect(group, &QButtonGroup::idClicked, [proc](int id) {
        curry_val argv[1] = { curry_make_fixnum(id) };
        SCHEME_CALL(curry_apply(proc, 1, argv));
    });
    container->setProperty("_qt6_bg", QVariant((quintptr)(void*)group));
    return widget_to_val(container);
}

static curry_val fn_radio_index(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QVariant v = val_to_widget(av[0])->property("_qt6_bg");
    if (!v.isValid()) curry_error("qt6: not a radio-group widget");
    return curry_make_fixnum(((QButtonGroup *)v.value<quintptr>())->checkedId());
}

/* (make-spin-box lo hi step initial callback) */
static curry_val fn_make_spin_box(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    auto *sb = new QDoubleSpinBox();
    sb->setRange(curry_float(av[0]), curry_float(av[1]));
    sb->setSingleStep(curry_float(av[2]));
    sb->setValue(curry_float(av[3]));
    curry_val proc = av[4];
    keep_alive(proc);
    QObject::connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                     [proc](double val) {
        curry_val argv[1] = { curry_make_float(val) };
        SCHEME_CALL(curry_apply(proc, 1, argv));
    });
    return widget_to_val(sb);
}

static curry_val fn_spin_value(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    return curry_make_float(((QDoubleSpinBox *)val_to_widget(av[0]))->value());
}
static curry_val fn_spin_set_value(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    ((QDoubleSpinBox *)val_to_widget(av[0]))->setValue(curry_float(av[1]));
    return curry_void();
}

/* (make-text-input placeholder callback) */
static curry_val fn_make_text_input(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    auto *le = new QLineEdit();
    le->setPlaceholderText(QString::fromUtf8(curry_string(av[0])));
    curry_val proc = av[1];
    keep_alive(proc);
    QObject::connect(le, &QLineEdit::textChanged, [proc](const QString &s) {
        QByteArray ba = s.toUtf8();
        curry_val argv[1] = { curry_make_string(ba.constData()) };
        SCHEME_CALL(curry_apply(proc, 1, argv));
    });
    return widget_to_val(le);
}

static curry_val fn_text_value(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QByteArray ba = ((QLineEdit *)val_to_widget(av[0]))->text().toUtf8();
    return curry_make_string(ba.constData());
}
static curry_val fn_text_set_value(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    ((QLineEdit *)val_to_widget(av[0]))->setText(QString::fromUtf8(curry_string(av[1])));
    return curry_void();
}

/* (make-progress-bar lo hi initial) */
static curry_val fn_make_progress(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    auto *pb = new QProgressBar();
    pb->setRange((int)curry_float(av[0]), (int)curry_float(av[1]));
    pb->setValue((int)curry_float(av[2]));
    return widget_to_val(pb);
}
static curry_val fn_progress_set(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    ((QProgressBar *)val_to_widget(av[0]))->setValue((int)curry_float(av[1]));
    return curry_void();
}

/* (make-separator) — horizontal rule */
static curry_val fn_make_separator(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;(void)av;
    auto *sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    sep->setFixedHeight(2);
    return widget_to_val(sep);
}

/* =========================================================================
 * Layer 3 — Menu system
 * ========================================================================= */

static curry_val fn_window_menubar(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    return menubar_to_val(val_to_win(av[0])->win->menuBar());
}
static curry_val fn_menubar_add_menu(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    return menu_to_val(
        val_to_menubar(av[0])->addMenu(QString::fromUtf8(curry_string(av[1]))));
}
/* (menu-add-action! menu title proc [shortcut-string]) */
static curry_val fn_menu_add_action(int ac, curry_val *av, void *ud) {
    (void)ud;
    QMenu  *menu = val_to_menu(av[0]);
    auto   *act  = menu->addAction(QString::fromUtf8(curry_string(av[1])));
    if (ac >= 4)
        act->setShortcut(QKeySequence(QString::fromUtf8(curry_string(av[3]))));
    curry_val proc = av[2];
    keep_alive(proc);
    QObject::connect(act, &QAction::triggered, [proc]() {
        SCHEME_CALL(curry_apply(proc, 0, nullptr));
    });
    return curry_void();
}
static curry_val fn_menu_add_submenu(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    return menu_to_val(
        val_to_menu(av[0])->addMenu(QString::fromUtf8(curry_string(av[1]))));
}
static curry_val fn_menu_add_separator(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; val_to_menu(av[0])->addSeparator(); return curry_void();
}

/* =========================================================================
 * Layer 3 — Toolbar
 * ========================================================================= */

static curry_val fn_window_add_toolbar(int ac, curry_val *av, void *ud) {
    (void)ud;
    auto *tb = new QToolBar();
    Qt::ToolBarArea area = Qt::TopToolBarArea;
    if (ac >= 2) {
        const char *side = curry_symbol(av[1]);
        if      (strcmp(side, "bottom") == 0) area = Qt::BottomToolBarArea;
        else if (strcmp(side, "left")   == 0) area = Qt::LeftToolBarArea;
        else if (strcmp(side, "right")  == 0) area = Qt::RightToolBarArea;
    }
    val_to_win(av[0])->win->addToolBar(area, tb);
    return ptr_to_val("qt6-toolbar", tb);
}
static curry_val fn_toolbar_add_action(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    auto *tb  = (QToolBar *)val_to_ptr("qt6-toolbar", av[0]);
    auto *act = tb->addAction(QString::fromUtf8(curry_string(av[1])));
    curry_val proc = av[2];
    keep_alive(proc);
    QObject::connect(act, &QAction::triggered, [proc]() {
        SCHEME_CALL(curry_apply(proc, 0, nullptr));
    });
    return curry_void();
}
static curry_val fn_toolbar_add_separator(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    ((QToolBar *)val_to_ptr("qt6-toolbar", av[0]))->addSeparator();
    return curry_void();
}

/* =========================================================================
 * Layer 3 — Status bar
 * ========================================================================= */

static curry_val fn_window_status_bar(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    return ptr_to_val("qt6-statusbar", val_to_win(av[0])->win->statusBar());
}
static curry_val fn_statusbar_set_text(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    ((QStatusBar *)val_to_ptr("qt6-statusbar", av[0]))
        ->showMessage(QString::fromUtf8(curry_string(av[1])));
    return curry_void();
}

/* =========================================================================
 * Layer 3 — Widget utilities
 * ========================================================================= */

static curry_val fn_widget_set_style(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    val_to_widget(av[0])->setStyleSheet(QString::fromUtf8(curry_string(av[1])));
    return curry_void();
}
static curry_val fn_widget_set_enabled(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    val_to_widget(av[0])->setEnabled(curry_bool(av[1])); return curry_void();
}
static curry_val fn_widget_set_visible(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    val_to_widget(av[0])->setVisible(curry_bool(av[1])); return curry_void();
}
static curry_val fn_widget_set_tooltip(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    val_to_widget(av[0])->setToolTip(QString::fromUtf8(curry_string(av[1])));
    return curry_void();
}
static curry_val fn_widget_set_min_size(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    val_to_widget(av[0])->setMinimumSize((int)curry_float(av[1]),
                                          (int)curry_float(av[2]));
    return curry_void();
}
static curry_val fn_widget_set_max_size(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    val_to_widget(av[0])->setMaximumSize((int)curry_float(av[1]),
                                          (int)curry_float(av[2]));
    return curry_void();
}

/* =========================================================================
 * Layer 3 — Timer
 * (make-timer interval-ms callback) → timer
 * (timer-start! timer)
 * (timer-stop! timer)
 * (timer-set-interval! timer ms)
 * ========================================================================= */

static curry_val fn_make_timer(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    int ms     = (int)curry_float(av[0]);
    curry_val proc = av[1];
    keep_alive(proc);
    auto *t    = new QTimer();
    t->setInterval(ms);
    QObject::connect(t, &QTimer::timeout, [proc]() {
        SCHEME_CALL(curry_apply(proc, 0, nullptr));
    });
    return timer_to_val(t);
}
static curry_val fn_timer_start(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; val_to_timer(av[0])->start(); return curry_void();
}
static curry_val fn_timer_stop(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; val_to_timer(av[0])->stop(); return curry_void();
}
static curry_val fn_timer_set_interval(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    val_to_timer(av[0])->setInterval((int)curry_float(av[1]));
    return curry_void();
}

/* =========================================================================
 * Layer 2 — gfx-* : GPU-accelerated 2D drawing via QPainter
 *
 * All gfx- functions take a painter as their first argument.
 * Painter is valid only inside a canvas-on-draw! callback.
 *
 * Color model: (r g b a) floats in [0,1].
 * gfx-set-color! sets both fill (brush) and stroke (pen) to the same color.
 * gfx-fill-*! operations draw with no outline.
 * gfx-draw-*! operations draw with no fill (outline only).
 *
 * Batch functions accept Scheme vectors of flonum coordinates for
 * efficient rendering of many primitives (particles, trajectories, meshes).
 * ========================================================================= */

#define P val_to_painter(av[0])

static curry_val fn_gfx_clear(int ac, curry_val *av, void *ud) {
    (void)ud;
    QPainter *p = P;
    QColor c = (ac >= 5) ? QColor::fromRgbF(curry_float(av[1]),curry_float(av[2]),
                                              curry_float(av[3]),curry_float(av[4]))
             : (ac >= 4) ? QColor::fromRgbF(curry_float(av[1]),curry_float(av[2]),
                                              curry_float(av[3]))
             : Qt::black;
    p->fillRect(0, 0, p->device()->width(), p->device()->height(), c);
    return curry_void();
}

static curry_val fn_gfx_set_color(int ac, curry_val *av, void *ud) {
    (void)ud;
    QPainter *p = P;
    double r = curry_float(av[1]), g = curry_float(av[2]), b = curry_float(av[3]);
    double a = (ac > 4) ? curry_float(av[4]) : 1.0;
    QColor c = QColor::fromRgbF(r, g, b, a);
    p->setPen(QPen(c, p->pen().widthF()));
    p->setBrush(QBrush(c));
    return curry_void();
}

static curry_val fn_gfx_set_pen_color(int ac, curry_val *av, void *ud) {
    (void)ud;
    QPainter *p = P;
    double a = (ac > 4) ? curry_float(av[4]) : 1.0;
    QPen pen = p->pen();
    pen.setColor(QColor::fromRgbF(curry_float(av[1]),curry_float(av[2]),
                                   curry_float(av[3]),a));
    p->setPen(pen);
    return curry_void();
}

static curry_val fn_gfx_set_pen_width(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P; QPen pen = p->pen();
    pen.setWidthF(curry_float(av[1])); p->setPen(pen);
    return curry_void();
}

static curry_val fn_gfx_set_antialias(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    P->setRenderHint(QPainter::Antialiasing, curry_bool(av[1]));
    return curry_void();
}

static curry_val fn_gfx_set_font(int ac, curry_val *av, void *ud) {
    (void)ud;
    QPainter *p = P; QFont f = p->font();
    if (ac > 1) f.setFamily(QString::fromUtf8(curry_string(av[1])));
    if (ac > 2) f.setPointSizeF(curry_float(av[2]));
    if (ac > 3) f.setBold(curry_bool(av[3]));
    if (ac > 4) f.setItalic(curry_bool(av[4]));
    p->setFont(f);
    return curry_void();
}

static curry_val fn_gfx_set_blend(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P;
    const char *m = curry_symbol(av[1]);
    QPainter::CompositionMode cm = QPainter::CompositionMode_SourceOver;
    if      (!strcmp(m,"add"))      cm = QPainter::CompositionMode_Plus;
    else if (!strcmp(m,"multiply")) cm = QPainter::CompositionMode_Multiply;
    else if (!strcmp(m,"screen"))   cm = QPainter::CompositionMode_Screen;
    else if (!strcmp(m,"overlay"))  cm = QPainter::CompositionMode_Overlay;
    else if (!strcmp(m,"clear"))    cm = QPainter::CompositionMode_Clear;
    else if (!strcmp(m,"src"))      cm = QPainter::CompositionMode_Source;
    p->setCompositionMode(cm);
    return curry_void();
}

static curry_val fn_gfx_save(int ac, curry_val *av, void *ud)    { (void)ud;(void)ac; P->save();    return curry_void(); }
static curry_val fn_gfx_restore(int ac, curry_val *av, void *ud) { (void)ud;(void)ac; P->restore(); return curry_void(); }

static curry_val fn_gfx_translate(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; P->translate(curry_float(av[1]),curry_float(av[2])); return curry_void(); }
static curry_val fn_gfx_rotate(int ac, curry_val *av, void *ud) {
    /* Accepts radians; QPainter uses degrees */
    (void)ud;(void)ac; P->rotate(curry_float(av[1])*(180.0/M_PI)); return curry_void(); }
static curry_val fn_gfx_scale(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; P->scale(curry_float(av[1]),curry_float(av[2])); return curry_void(); }

/* --- Shapes --- */

static curry_val fn_gfx_fill_rect(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P;
    p->fillRect(QRectF(curry_float(av[1]),curry_float(av[2]),
                       curry_float(av[3]),curry_float(av[4])), p->brush());
    return curry_void();
}

static curry_val fn_gfx_draw_rect(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P; p->save(); p->setBrush(Qt::NoBrush);
    p->drawRect(QRectF(curry_float(av[1]),curry_float(av[2]),
                       curry_float(av[3]),curry_float(av[4])));
    p->restore(); return curry_void();
}

static curry_val fn_gfx_fill_circle(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P; p->save(); p->setPen(Qt::NoPen);
    p->drawEllipse(QPointF(curry_float(av[1]),curry_float(av[2])),
                   curry_float(av[3]),curry_float(av[3]));
    p->restore(); return curry_void();
}

static curry_val fn_gfx_draw_circle(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P; p->save(); p->setBrush(Qt::NoBrush);
    p->drawEllipse(QPointF(curry_float(av[1]),curry_float(av[2])),
                   curry_float(av[3]),curry_float(av[3]));
    p->restore(); return curry_void();
}

static curry_val fn_gfx_fill_ellipse(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P; p->save(); p->setPen(Qt::NoPen);
    p->drawEllipse(QPointF(curry_float(av[1]),curry_float(av[2])),
                   curry_float(av[3]),curry_float(av[4]));
    p->restore(); return curry_void();
}

static curry_val fn_gfx_draw_ellipse(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P; p->save(); p->setBrush(Qt::NoBrush);
    p->drawEllipse(QPointF(curry_float(av[1]),curry_float(av[2])),
                   curry_float(av[3]),curry_float(av[4]));
    p->restore(); return curry_void();
}

static curry_val fn_gfx_draw_line(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    P->drawLine(QPointF(curry_float(av[1]),curry_float(av[2])),
                QPointF(curry_float(av[3]),curry_float(av[4])));
    return curry_void();
}

/* Arc: angles in radians.  0 = East (3 o'clock), positive = CCW. */
static curry_val fn_gfx_draw_arc(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P;
    double cx = curry_float(av[1]), cy = curry_float(av[2]), r = curry_float(av[3]);
    int start = (int)(curry_float(av[4]) * (180.0/M_PI) * 16.0);
    int span  = (int)(curry_float(av[5]) * (180.0/M_PI) * 16.0);
    p->save(); p->setBrush(Qt::NoBrush);
    p->drawArc(QRectF(cx-r, cy-r, r*2, r*2), start, span);
    p->restore(); return curry_void();
}

static curry_val fn_gfx_fill_pie(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P;
    double cx = curry_float(av[1]), cy = curry_float(av[2]), r = curry_float(av[3]);
    int start = (int)(curry_float(av[4]) * (180.0/M_PI) * 16.0);
    int span  = (int)(curry_float(av[5]) * (180.0/M_PI) * 16.0);
    p->save(); p->setPen(Qt::NoPen);
    p->drawPie(QRectF(cx-r, cy-r, r*2, r*2), start, span);
    p->restore(); return curry_void();
}

/* Polygon — points is a Scheme list of (x . y) pairs */
static curry_val fn_gfx_fill_polygon(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P; QPolygonF poly;
    for (curry_val pts = av[1]; !curry_is_nil(pts); pts = curry_cdr(pts)) {
        curry_val pt = curry_car(pts);
        poly << QPointF(curry_float(curry_car(pt)), curry_float(curry_cdr(pt)));
    }
    p->save(); p->setPen(Qt::NoPen); p->drawPolygon(poly); p->restore();
    return curry_void();
}

static curry_val fn_gfx_draw_polygon(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter *p = P; QPolygonF poly;
    for (curry_val pts = av[1]; !curry_is_nil(pts); pts = curry_cdr(pts)) {
        curry_val pt = curry_car(pts);
        poly << QPointF(curry_float(curry_car(pt)), curry_float(curry_cdr(pt)));
    }
    p->save(); p->setBrush(Qt::NoBrush); p->drawPolygon(poly); p->restore();
    return curry_void();
}

/* Text */
static curry_val fn_gfx_draw_text(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    if (!curry_is_string(av[3]))
        curry_error("gfx-draw-text!: arg 4 must be a string (got wrong type) — usage: (gfx-draw-text! painter x y text)");
    P->drawText(QPointF(curry_float(av[1]),curry_float(av[2])),
                QString::fromUtf8(curry_string(av[3])));
    return curry_void();
}

/* Blit a raw RGBA image from a bytevector */
static curry_val fn_gfx_draw_image(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    /* (gfx-draw-image! painter dst-x dst-y dst-w dst-h bvec img-w img-h) */
    QPainter  *p  = P;
    double    dx = curry_float(av[1]), dy = curry_float(av[2]);
    double    dw = curry_float(av[3]), dh = curry_float(av[4]);
    curry_val bv = av[5];
    int       iw = (int)curry_float(av[6]), ih = (int)curry_float(av[7]);
    QImage img(iw, ih, QImage::Format_RGBA8888);
    int total = iw * ih * 4;
    for (int i = 0; i < total; i++)
        img.bits()[i] = curry_bytevector_ref(bv, (uint32_t)i);
    p->drawImage(QRectF(dx,dy,dw,dh), img);
    return curry_void();
}

/* --- Batch drawing for high-performance particle/trajectory rendering ---
 *
 * (gfx-draw-points! painter xvec yvec r g b a size)
 *   xvec, yvec — Scheme vectors of flonum x and y coordinates (same length)
 *   size        — diameter in pixels
 *
 * (gfx-draw-lines! painter coords-vec r g b a width)
 *   coords-vec — flat Scheme vector [x0 y0 x1 y1 x2 y2 x3 y3 ...]
 *   Each consecutive pair (x2i,y2i)→(x2i+1,y2i+1) is one line segment.
 *
 * (gfx-fill-triangles! painter coords-vec r g b a)
 *   coords-vec — flat Scheme vector [x0 y0 x1 y1 x2 y2 ...]
 *   Every 6 values = one filled triangle (3 × (x,y)).
 */

static curry_val fn_gfx_draw_points(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter  *p  = P;
    curry_val  xv = av[1], yv = av[2];
    double r = curry_float(av[3]), g = curry_float(av[4]);
    double b = curry_float(av[5]), a = curry_float(av[6]);
    double sz = curry_float(av[7]);
    uint32_t n = curry_vector_length(xv);
    p->save();
    p->setPen(Qt::NoPen);
    p->setBrush(QColor::fromRgbF(r,g,b,a));
    double hs = sz * 0.5;
    for (uint32_t i = 0; i < n; i++) {
        double x = curry_float(curry_vector_ref(xv, i));
        double y = curry_float(curry_vector_ref(yv, i));
        p->drawEllipse(QPointF(x,y), hs, hs);
    }
    p->restore();
    return curry_void();
}

static curry_val fn_gfx_draw_lines(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter  *p  = P;
    curry_val  cv = av[1];
    double r = curry_float(av[2]), g = curry_float(av[3]);
    double b = curry_float(av[4]), a = curry_float(av[5]);
    double w = curry_float(av[6]);
    uint32_t n = curry_vector_length(cv);
    QVector<QLineF> lines;
    lines.reserve((int)(n/4));
    for (uint32_t i = 0; i + 3 < n; i += 4) {
        lines.append(QLineF(curry_float(curry_vector_ref(cv,i)),
                            curry_float(curry_vector_ref(cv,i+1)),
                            curry_float(curry_vector_ref(cv,i+2)),
                            curry_float(curry_vector_ref(cv,i+3))));
    }
    p->save();
    p->setPen(QPen(QColor::fromRgbF(r,g,b,a), w));
    p->drawLines(lines);
    p->restore();
    return curry_void();
}

static curry_val fn_gfx_fill_triangles(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    QPainter  *p  = P;
    curry_val  cv = av[1];
    double r = curry_float(av[2]), g = curry_float(av[3]);
    double b = curry_float(av[4]), a = curry_float(av[5]);
    uint32_t n = curry_vector_length(cv);
    p->save();
    p->setPen(Qt::NoPen);
    p->setBrush(QColor::fromRgbF(r,g,b,a));
    for (uint32_t i = 0; i + 5 < n; i += 6) {
        QPointF pts[3] = {
            {curry_float(curry_vector_ref(cv,i)),   curry_float(curry_vector_ref(cv,i+1))},
            {curry_float(curry_vector_ref(cv,i+2)), curry_float(curry_vector_ref(cv,i+3))},
            {curry_float(curry_vector_ref(cv,i+4)), curry_float(curry_vector_ref(cv,i+5))}
        };
        p->drawPolygon(pts, 3);
    }
    p->restore();
    return curry_void();
}

#undef P

/* =========================================================================
 * Layer 1 — qt-* : raw Qt6 queries
 * ========================================================================= */

static curry_val fn_qt_painter_width(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    return curry_make_fixnum(val_to_painter(av[0])->device()->width());
}
static curry_val fn_qt_painter_height(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    return curry_make_fixnum(val_to_painter(av[0])->device()->height());
}
static curry_val fn_qt_widget_width(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; return curry_make_fixnum(val_to_widget(av[0])->width());
}
static curry_val fn_qt_widget_height(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac; return curry_make_fixnum(val_to_widget(av[0])->height());
}
/* (qt-gpu? canvas) → #t if OpenGL initialised successfully */
static curry_val fn_qt_gpu_p(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    return curry_make_bool(val_to_canvas(av[0])->gpu_ok);
}
/* (qt-process-events) — flush pending Qt events; useful inside simulation loops */
static curry_val fn_qt_process_events(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;(void)av;
    if (s_app) s_app->processEvents();
    return curry_void();
}

/* =========================================================================
 * 4D projection math (no Qt dependency — preserved from original stub)
 * ========================================================================= */

static void project_4d_to_3d(const double *p4, double fov4d, double *p3) {
    double factor = fov4d / (fov4d - p4[3]);
    p3[0] = p4[0] * factor;
    p3[1] = p4[1] * factor;
    p3[2] = p4[2] * factor;
}

static curry_val fn_make_4d_projector(int ac, curry_val *av, void *ud) {
    (void)ud;
    double fov4d = ac > 0 ? curry_float(av[0]) : 4.0;
    double fov3d = ac > 1 ? curry_float(av[1]) : 3.0;
    curry_val v = curry_make_vector(2, curry_make_bool(false));
    curry_vector_set(v, 0, curry_make_float(fov4d));
    curry_vector_set(v, 1, curry_make_float(fov3d));
    return v;
}

static curry_val fn_project_4d(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    double fov4d = curry_float(curry_vector_ref(av[0], 0));
    double pts[4];
    for (int i = 0; i < 4; i++) pts[i] = curry_float(curry_vector_ref(av[1],(uint32_t)i));
    double p3[3]; project_4d_to_3d(pts, fov4d, p3);
    curry_val r = curry_make_vector(3, curry_make_bool(false));
    for (int i = 0; i < 3; i++) curry_vector_set(r,(uint32_t)i,curry_make_float(p3[i]));
    return r;
}

static curry_val fn_rotate_4d_xw(int ac, curry_val *av, void *ud) {
    (void)ud;(void)ac;
    double pts[4];
    for (int i = 0; i < 4; i++) pts[i] = curry_float(curry_vector_ref(av[0],(uint32_t)i));
    double c = cos(curry_float(av[1])), s = sin(curry_float(av[1]));
    curry_val r = curry_make_vector(4, curry_make_bool(false));
    curry_vector_set(r, 0, curry_make_float(c*pts[0] - s*pts[3]));
    curry_vector_set(r, 1, curry_make_float(pts[1]));
    curry_vector_set(r, 2, curry_make_float(pts[2]));
    curry_vector_set(r, 3, curry_make_float(s*pts[0] + c*pts[3]));
    return r;
}

/* =========================================================================
 * Module registration
 * ========================================================================= */

extern "C" void curry_module_init(CurryVM *vm) {

    /* Anchor the proc-roots list on the GC heap so Boehm can trace it */
    s_proc_roots = curry_nil();

    /* --- Layer 3: Window --- */
    curry_define_fn(vm, "make-window",          fn_make_window,         3, 3, NULL);
    curry_define_fn(vm, "window-on-close!",     fn_window_on_close,     2, 2, NULL);
    curry_define_fn(vm, "window-on-key!",       fn_window_on_key,       2, 2, NULL);
    curry_define_fn(vm, "window-on-realize!",   fn_window_on_realize,   2, 2, NULL);
    curry_define_fn(vm, "window-canvas",        fn_window_canvas,       1, 1, NULL);
    curry_define_fn(vm, "window-sidebar",       fn_window_sidebar,      1, 1, NULL);
    curry_define_fn(vm, "window-show!",         fn_window_show,         1, 1, NULL);
    curry_define_fn(vm, "window-hide!",         fn_window_hide,         1, 1, NULL);
    curry_define_fn(vm, "window-set-title!",    fn_window_set_title,    2, 2, NULL);
    curry_define_fn(vm, "run-event-loop",       fn_run_event_loop,      0, 0, NULL);
    curry_define_fn(vm, "quit-event-loop",      fn_quit_event_loop,     0, 0, NULL);

    /* --- Layer 3: Canvas --- */
    curry_define_fn(vm, "canvas-on-draw!",      fn_canvas_on_draw,      2, 2, NULL);
    curry_define_fn(vm, "canvas-on-mouse!",     fn_canvas_on_mouse,     2, 2, NULL);
    curry_define_fn(vm, "canvas-redraw!",       fn_canvas_redraw,       1, 1, NULL);

    /* --- Layer 3: Layout --- */
    curry_define_fn(vm, "box-add!",             fn_box_add,             2, 2, NULL);
    curry_define_fn(vm, "make-vbox",            fn_make_vbox,           0, 0, NULL);
    curry_define_fn(vm, "make-hbox",            fn_make_hbox,           0, 0, NULL);
    curry_define_fn(vm, "layout-add!",          fn_layout_add,          2, 2, NULL);
    curry_define_fn(vm, "make-tabs",            fn_make_tabs,           0, 0, NULL);
    curry_define_fn(vm, "tabs-add!",            fn_tabs_add,            3, 3, NULL);
    curry_define_fn(vm, "make-group-box",       fn_make_group_box,      1, 1, NULL);

    /* --- Layer 3: Widgets --- */
    curry_define_fn(vm, "make-label",           fn_make_label,          1, 1, NULL);
    curry_define_fn(vm, "label-set-text!",      fn_label_set_text,      2, 2, NULL);
    curry_define_fn(vm, "make-button",          fn_make_button,         2, 2, NULL);
    curry_define_fn(vm, "make-toggle",          fn_make_toggle,         3, 3, NULL);
    curry_define_fn(vm, "make-slider",          fn_make_slider,         5, 6, NULL);
    curry_define_fn(vm, "slider-value",         fn_slider_value,        1, 1, NULL);
    curry_define_fn(vm, "slider-set-value!",    fn_slider_set_value,    2, 2, NULL);
    curry_define_fn(vm, "make-dropdown",        fn_make_dropdown,       3, 3, NULL);
    curry_define_fn(vm, "dropdown-index",       fn_dropdown_index,      1, 1, NULL);
    curry_define_fn(vm, "dropdown-set-index!",  fn_dropdown_set_index,  2, 2, NULL);
    curry_define_fn(vm, "dropdown-selected",    fn_dropdown_selected,   1, 1, NULL);
    curry_define_fn(vm, "make-radio-group",     fn_make_radio_group,    3, 3, NULL);
    curry_define_fn(vm, "radio-index",          fn_radio_index,         1, 1, NULL);
    curry_define_fn(vm, "make-spin-box",        fn_make_spin_box,       5, 5, NULL);
    curry_define_fn(vm, "spin-value",           fn_spin_value,          1, 1, NULL);
    curry_define_fn(vm, "spin-set-value!",      fn_spin_set_value,      2, 2, NULL);
    curry_define_fn(vm, "make-text-input",      fn_make_text_input,     2, 2, NULL);
    curry_define_fn(vm, "text-value",           fn_text_value,          1, 1, NULL);
    curry_define_fn(vm, "text-set-value!",      fn_text_set_value,      2, 2, NULL);
    curry_define_fn(vm, "make-progress-bar",    fn_make_progress,       3, 3, NULL);
    curry_define_fn(vm, "progress-set!",        fn_progress_set,        2, 2, NULL);
    curry_define_fn(vm, "make-separator",       fn_make_separator,      0, 0, NULL);
    curry_define_fn(vm, "widget-set-style!",    fn_widget_set_style,    2, 2, NULL);
    curry_define_fn(vm, "widget-set-enabled!",  fn_widget_set_enabled,  2, 2, NULL);
    curry_define_fn(vm, "widget-set-visible!",  fn_widget_set_visible,  2, 2, NULL);
    curry_define_fn(vm, "widget-set-tooltip!",  fn_widget_set_tooltip,  2, 2, NULL);
    curry_define_fn(vm, "widget-set-min-size!", fn_widget_set_min_size, 3, 3, NULL);
    curry_define_fn(vm, "widget-set-max-size!", fn_widget_set_max_size, 3, 3, NULL);

    /* --- Layer 3: Menus --- */
    curry_define_fn(vm, "window-menu-bar",      fn_window_menubar,      1, 1, NULL);
    curry_define_fn(vm, "menubar-add-menu!",    fn_menubar_add_menu,    2, 2, NULL);
    curry_define_fn(vm, "menu-add-action!",     fn_menu_add_action,     3, 4, NULL);
    curry_define_fn(vm, "menu-add-menu!",       fn_menu_add_submenu,    2, 2, NULL);
    curry_define_fn(vm, "menu-add-separator!",  fn_menu_add_separator,  1, 1, NULL);

    /* --- Layer 3: Toolbar --- */
    curry_define_fn(vm, "window-add-toolbar!",  fn_window_add_toolbar,  1, 2, NULL);
    curry_define_fn(vm, "toolbar-add-action!",  fn_toolbar_add_action,  3, 3, NULL);
    curry_define_fn(vm, "toolbar-add-separator!",fn_toolbar_add_separator,1,1,NULL);

    /* --- Layer 3: Status bar --- */
    curry_define_fn(vm, "window-status-bar",    fn_window_status_bar,   1, 1, NULL);
    curry_define_fn(vm, "statusbar-set-text!",  fn_statusbar_set_text,  2, 2, NULL);

    /* --- Layer 3: Timer --- */
    curry_define_fn(vm, "make-timer",           fn_make_timer,          2, 2, NULL);
    curry_define_fn(vm, "timer-start!",         fn_timer_start,         1, 1, NULL);
    curry_define_fn(vm, "timer-stop!",          fn_timer_stop,          1, 1, NULL);
    curry_define_fn(vm, "timer-set-interval!",  fn_timer_set_interval,  2, 2, NULL);

    /* --- Layer 2: gfx-* graphics --- */
    curry_define_fn(vm, "gfx-clear!",           fn_gfx_clear,           1, 5, NULL);
    curry_define_fn(vm, "gfx-set-color!",       fn_gfx_set_color,       4, 5, NULL);
    curry_define_fn(vm, "gfx-set-pen-color!",   fn_gfx_set_pen_color,   4, 5, NULL);
    curry_define_fn(vm, "gfx-set-pen-width!",   fn_gfx_set_pen_width,   2, 2, NULL);
    curry_define_fn(vm, "gfx-set-antialias!",   fn_gfx_set_antialias,   2, 2, NULL);
    curry_define_fn(vm, "gfx-set-font!",        fn_gfx_set_font,        1, 5, NULL);
    curry_define_fn(vm, "gfx-set-blend!",       fn_gfx_set_blend,       2, 2, NULL);
    curry_define_fn(vm, "gfx-save!",            fn_gfx_save,            1, 1, NULL);
    curry_define_fn(vm, "gfx-restore!",         fn_gfx_restore,         1, 1, NULL);
    curry_define_fn(vm, "gfx-translate!",       fn_gfx_translate,       3, 3, NULL);
    curry_define_fn(vm, "gfx-rotate!",          fn_gfx_rotate,          2, 2, NULL);
    curry_define_fn(vm, "gfx-scale!",           fn_gfx_scale,           3, 3, NULL);
    curry_define_fn(vm, "gfx-fill-rect!",       fn_gfx_fill_rect,       5, 5, NULL);
    curry_define_fn(vm, "gfx-draw-rect!",       fn_gfx_draw_rect,       5, 5, NULL);
    curry_define_fn(vm, "gfx-fill-circle!",     fn_gfx_fill_circle,     4, 4, NULL);
    curry_define_fn(vm, "gfx-draw-circle!",     fn_gfx_draw_circle,     4, 4, NULL);
    curry_define_fn(vm, "gfx-fill-ellipse!",    fn_gfx_fill_ellipse,    5, 5, NULL);
    curry_define_fn(vm, "gfx-draw-ellipse!",    fn_gfx_draw_ellipse,    5, 5, NULL);
    curry_define_fn(vm, "gfx-draw-line!",       fn_gfx_draw_line,       5, 5, NULL);
    curry_define_fn(vm, "gfx-draw-arc!",        fn_gfx_draw_arc,        6, 6, NULL);
    curry_define_fn(vm, "gfx-fill-pie!",        fn_gfx_fill_pie,        6, 6, NULL);
    curry_define_fn(vm, "gfx-fill-polygon!",    fn_gfx_fill_polygon,    2, 2, NULL);
    curry_define_fn(vm, "gfx-draw-polygon!",    fn_gfx_draw_polygon,    2, 2, NULL);
    curry_define_fn(vm, "gfx-draw-text!",       fn_gfx_draw_text,       4, 4, NULL);
    curry_define_fn(vm, "gfx-draw-image!",      fn_gfx_draw_image,      8, 8, NULL);
    curry_define_fn(vm, "gfx-draw-points!",     fn_gfx_draw_points,     8, 8, NULL);
    curry_define_fn(vm, "gfx-draw-lines!",      fn_gfx_draw_lines,      7, 7, NULL);
    curry_define_fn(vm, "gfx-fill-triangles!",  fn_gfx_fill_triangles,  6, 6, NULL);

    /* --- Layer 1: qt-* raw --- */
    curry_define_fn(vm, "qt-painter-width",     fn_qt_painter_width,    1, 1, NULL);
    curry_define_fn(vm, "qt-painter-height",    fn_qt_painter_height,   1, 1, NULL);
    curry_define_fn(vm, "qt-widget-width",      fn_qt_widget_width,     1, 1, NULL);
    curry_define_fn(vm, "qt-widget-height",     fn_qt_widget_height,    1, 1, NULL);
    curry_define_fn(vm, "qt-gpu?",              fn_qt_gpu_p,            1, 1, NULL);
    curry_define_fn(vm, "qt-process-events",    fn_qt_process_events,   0, 0, NULL);

    /* --- 4D projection --- */
    curry_define_fn(vm, "make-4d-projector",    fn_make_4d_projector,   0, 2, NULL);
    curry_define_fn(vm, "project-4d",           fn_project_4d,          2, 2, NULL);
    curry_define_fn(vm, "rotate-4d-xw",         fn_rotate_4d_xw,        2, 2, NULL);
}
