#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>

#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "twh.h"

#define SURFACE_CHANNELS 4

struct twh_window
{
    Window handle;
    XImage *ximage;

    int surface_w;
    int surface_h;
    unsigned char *surface;

    int should_close;
    void *userdata;

    twh_key_callback_func_t key_callback;
    twh_mouse_callback_func_t mouse_callback;
    twh_scroll_callback_func_t scroll_callback;
};

static Display *g_display = NULL;
static XContext g_context;
static int g_key_code_table[0xffff] = {0};

/* declarations */
static void open_display();
static void close_display();
static double get_native_time();

static Window create_linux_window(const char *titile, int width, int height);
static void create_key_code_table();
static void create_surface(int width, int height, unsigned char **out_surface, XImage **out_ximage);

static void present_surface(twh_window_t *wnd);
static void blit_bgr(unsigned char *src, int src_w, int src_h, unsigned char *dst, int dst_w, int dst_h);

static TWH_KEY_CODE get_key_code(int virtual_key);
static void handle_key_event(twh_window_t *wnd, int virtual_key, char pressed);
static void handle_mouse_event(twh_window_t *wnd, int xbutton, char pressed);
static void handle_client_event(twh_window_t *wnd, XClientMessageEvent *event);
static void process_event(XEvent *event);

/* implementaions */

void twh_init(void)
{
    assert(g_display == NULL);
    open_display();
}

void twh_terminate(void)
{
    assert(g_display != NULL);
    close_display();
}

float twh_get_timef(void)
{
    static double initial = -1;
    if (initial < 0)
    {
        initial = get_native_time();
    }
    return (float)(get_native_time() - initial);
}

twh_window_t *twh_window_create(const char *title, int width, int height)
{
    twh_window_t *window = NULL;
    Window handle;
    unsigned char *surface = NULL;
    XImage *ximage = NULL;

    assert(g_display && width > 0 && height > 0);

    handle = create_linux_window(title, width, height);
    create_key_code_table();
    create_surface(width, height, &surface, &ximage);

    window = (twh_window_t *)malloc(sizeof(twh_window_t));
    memset(window, 0, sizeof(twh_window_t));
    window->handle = handle;
    window->ximage = ximage;
    window->surface_w = width;
    window->surface_h = height;
    window->surface = surface;

    XSaveContext(g_display, handle, g_context, (XPointer)window);
    XMapWindow(g_display, handle);
    XFlush(g_display);
    return window;
}

void twh_window_release(twh_window_t *wnd)
{
    if (wnd == NULL)
        return;

    XUnmapWindow(g_display, wnd->handle);
    XDeleteContext(g_display, wnd->handle, g_context);

    wnd->ximage->data = NULL;
    XDestroyImage(wnd->ximage);
    XDestroyWindow(g_display, wnd->handle);
    XFlush(g_display);

    if (wnd->surface != NULL)
        free(wnd->surface);
    free(wnd);
    wnd = NULL;
}

void twh_set_user_data(twh_window_t *wnd, void *userdata)
{
    wnd->userdata = userdata;
}

void *twh_get_user_data(twh_window_t *wnd)
{
    return wnd->userdata;
}

int twh_window_should_close(twh_window_t *wnd)
{
    return wnd->should_close;
}

void twh_window_close(twh_window_t *wnd)
{
    wnd->should_close = 1;
}

void twh_poll_events()
{
    XPending(g_display);
    while (XQLength(g_display))
    {
        XEvent event;
        XNextEvent(g_display, &event);
        process_event(&event);
    }
    XFlush(g_display);
}

void twh_set_key_callback(twh_window_t *wnd, twh_key_callback_func_t key_callback)
{
    wnd->key_callback = key_callback;
}

void twh_set_mouse_callback(twh_window_t *wnd, twh_mouse_callback_func_t mouse_callback)
{
    wnd->mouse_callback = mouse_callback;
}

void twh_set_scroll_callback(twh_window_t *wnd, twh_scroll_callback_func_t scroll_callback)
{
    wnd->scroll_callback = scroll_callback;
}

void twh_get_cursor_pos(twh_window_t *wnd, float *xpos, float *ypos)
{
    Window root, child;
    int root_x, root_y, window_x, window_y;
    unsigned int mask;
    XQueryPointer(g_display, wnd->handle, &root, &child,
                  &root_x, &root_y, &window_x, &window_y, &mask);
    *xpos = (float)window_x;
    *ypos = (float)window_y;
}

twh_framebuffer_t *twh_framebuffer_create(int width, int height)
{
    twh_framebuffer_t *framebuffer = (twh_framebuffer_t *)malloc(sizeof(twh_framebuffer_t));
    framebuffer->width = width;
    framebuffer->height = height;
    size_t sz = width * height * sizeof(unsigned char) * SURFACE_CHANNELS;
    framebuffer->buffer = (unsigned char *)malloc(sz);
    memset(framebuffer->buffer, 0, sz);
    return framebuffer;
}

void twh_framebuffer_release(twh_framebuffer_t *fb)
{
    if (fb != NULL)
    {
        if (fb->buffer != NULL)
        {
            free(fb->buffer);
            fb->buffer = NULL;
        }
        free(fb);
        fb = NULL;
    }
}

void twh_framebuffer_set_color_u8(twh_framebuffer_t *fb, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    int index = (y * fb->width + x) * SURFACE_CHANNELS;
    fb->buffer[index + 0] = r;
    fb->buffer[index + 1] = g;
    fb->buffer[index + 2] = b;
}

void twh_framebuffer_set_color_u32(twh_framebuffer_t *fb, int x, int y, uint32_t rgb)
{
    int index = (y * fb->width + x) * SURFACE_CHANNELS;
    fb->buffer[index + 0] = (rgb >> 16) & 0xff;
    fb->buffer[index + 1] = (rgb >> 8) & 0xff;
    fb->buffer[index + 2] = rgb & 0xff;
}

void twh_framebuffer_render(twh_window_t *wnd, twh_framebuffer_t *fb)
{
    blit_bgr(fb->buffer, fb->width, fb->height, wnd->surface, wnd->surface_w, wnd->surface_h);
    present_surface(wnd);
}

/* private functions */
static void open_display()
{
    g_display = XOpenDisplay(NULL);
    assert(g_display != NULL);
    g_context = XUniqueContext();
}

static void close_display()
{
    XCloseDisplay(g_display);
    g_display = NULL;
}

static double get_native_time()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static Window create_linux_window(const char *title, int width, int height)
{
    int screen = XDefaultScreen(g_display);
    unsigned long border = XWhitePixel(g_display, screen);
    unsigned long background = XBlackPixel(g_display, screen);
    Window root = XRootWindow(g_display, screen);
    Window handle;
    XSizeHints *size_hints;
    XClassHint *class_hint;
    Atom delete_window;
    long mask;

    handle = XCreateSimpleWindow(g_display, root, 0, 0, width, height, 0,
                                 border, background);

    /* not resizable */
    size_hints = XAllocSizeHints();
    size_hints->flags = PMinSize | PMaxSize;
    size_hints->min_width = width;
    size_hints->max_width = width;
    size_hints->min_height = height;
    size_hints->max_height = height;
    XSetWMNormalHints(g_display, handle, size_hints);
    XFree(size_hints);

    /* application name */
    class_hint = XAllocClassHint();
    class_hint->res_name = (char *)title;
    class_hint->res_class = (char *)title;
    XSetClassHint(g_display, handle, class_hint);
    XFree(class_hint);

    /* event subscription */
    mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask;
    XSelectInput(g_display, handle, mask);
    delete_window = XInternAtom(g_display, "WM_DELETE_WINDOW", True);
    XSetWMProtocols(g_display, handle, &delete_window, 1);

    return handle;
}

/*
 * TODO: Some key codes are not working...
 */
static void create_key_code_table()
{
    /* initialize */
    memset(g_key_code_table, TWH_KEY_NUM, 0xffff * sizeof(int));

    /* Number 1 ~ 9 */
    g_key_code_table[XK_0] = TWH_KEY_0;
    g_key_code_table[XK_1] = TWH_KEY_1;
    g_key_code_table[XK_2] = TWH_KEY_2;
    g_key_code_table[XK_3] = TWH_KEY_3;
    g_key_code_table[XK_4] = TWH_KEY_4;
    g_key_code_table[XK_5] = TWH_KEY_5;
    g_key_code_table[XK_6] = TWH_KEY_6;
    g_key_code_table[XK_7] = TWH_KEY_7;
    g_key_code_table[XK_8] = TWH_KEY_8;
    g_key_code_table[XK_9] = TWH_KEY_9;

    /* Alphabet */
    g_key_code_table[XK_a] = TWH_KEY_A;
    g_key_code_table[XK_b] = TWH_KEY_B;
    g_key_code_table[XK_c] = TWH_KEY_C;
    g_key_code_table[XK_d] = TWH_KEY_D;
    g_key_code_table[XK_e] = TWH_KEY_E;
    g_key_code_table[XK_f] = TWH_KEY_F;
    g_key_code_table[XK_g] = TWH_KEY_G;
    g_key_code_table[XK_h] = TWH_KEY_H;
    g_key_code_table[XK_i] = TWH_KEY_I;
    g_key_code_table[XK_j] = TWH_KEY_J;
    g_key_code_table[XK_k] = TWH_KEY_K;
    g_key_code_table[XK_l] = TWH_KEY_L;
    g_key_code_table[XK_m] = TWH_KEY_M;
    g_key_code_table[XK_n] = TWH_KEY_N;
    g_key_code_table[XK_o] = TWH_KEY_O;
    g_key_code_table[XK_p] = TWH_KEY_P;
    g_key_code_table[XK_q] = TWH_KEY_Q;
    g_key_code_table[XK_r] = TWH_KEY_R;
    g_key_code_table[XK_s] = TWH_KEY_S;
    g_key_code_table[XK_t] = TWH_KEY_T;
    g_key_code_table[XK_u] = TWH_KEY_U;
    g_key_code_table[XK_v] = TWH_KEY_V;
    g_key_code_table[XK_w] = TWH_KEY_W;
    g_key_code_table[XK_x] = TWH_KEY_X;
    g_key_code_table[XK_y] = TWH_KEY_Y;
    g_key_code_table[XK_z] = TWH_KEY_Z;

    /* Num pad numbers */
    g_key_code_table[XK_KP_0] = TWH_KEY_NUMPAD_0;
    g_key_code_table[XK_KP_1] = TWH_KEY_NUMPAD_1;
    g_key_code_table[XK_KP_2] = TWH_KEY_NUMPAD_2;
    g_key_code_table[XK_KP_3] = TWH_KEY_NUMPAD_3;
    g_key_code_table[XK_KP_4] = TWH_KEY_NUMPAD_4;
    g_key_code_table[XK_KP_5] = TWH_KEY_NUMPAD_5;
    g_key_code_table[XK_KP_6] = TWH_KEY_NUMPAD_6;
    g_key_code_table[XK_KP_7] = TWH_KEY_NUMPAD_7;
    g_key_code_table[XK_KP_8] = TWH_KEY_NUMPAD_8;
    g_key_code_table[XK_KP_9] = TWH_KEY_NUMPAD_9;

    /* F1 ~ F24 */
    g_key_code_table[XK_F1] = TWH_KEY_F1;
    g_key_code_table[XK_F2] = TWH_KEY_F2;
    g_key_code_table[XK_F3] = TWH_KEY_F3;
    g_key_code_table[XK_F4] = TWH_KEY_F4;
    g_key_code_table[XK_F5] = TWH_KEY_F5;
    g_key_code_table[XK_F6] = TWH_KEY_F6;
    g_key_code_table[XK_F7] = TWH_KEY_F7;
    g_key_code_table[XK_F8] = TWH_KEY_F8;
    g_key_code_table[XK_F9] = TWH_KEY_F9;
    g_key_code_table[XK_F10] = TWH_KEY_F10;
    g_key_code_table[XK_F11] = TWH_KEY_F11;
    g_key_code_table[XK_F12] = TWH_KEY_F12;
    g_key_code_table[XK_F13] = TWH_KEY_F13;
    g_key_code_table[XK_F14] = TWH_KEY_F14;
    g_key_code_table[XK_F15] = TWH_KEY_F15;
    g_key_code_table[XK_F16] = TWH_KEY_F16;
    g_key_code_table[XK_F17] = TWH_KEY_F17;
    g_key_code_table[XK_F18] = TWH_KEY_F18;
    g_key_code_table[XK_F19] = TWH_KEY_F19;
    g_key_code_table[XK_F20] = TWH_KEY_F20;
    g_key_code_table[XK_F21] = TWH_KEY_F21;
    g_key_code_table[XK_F22] = TWH_KEY_F22;
    g_key_code_table[XK_F23] = TWH_KEY_F23;
    g_key_code_table[XK_F24] = TWH_KEY_F24;

    /* Functions */

    g_key_code_table[XK_BackSpace] = TWH_KEY_BACKSPACE;
    g_key_code_table[XK_Tab] = TWH_KEY_TAB;
    g_key_code_table[XK_Return] = TWH_KEY_ENTER;
    g_key_code_table[XK_Pause] = TWH_KEY_PAUSE;
    g_key_code_table[XK_Scroll_Lock] = TWH_KEY_SCROLL_LOCK;
    g_key_code_table[XK_Print] = TWH_KEY_PRINT_SCREEN;
    g_key_code_table[XK_Caps_Lock] = TWH_KEY_CAPS_LOCK;
    g_key_code_table[XK_Escape] = TWH_KEY_ESCAPE;
    g_key_code_table[XK_space] = TWH_KEY_SPACE;
    g_key_code_table[XK_Page_Up] = TWH_KEY_PAGE_UP;
    g_key_code_table[XK_Page_Down] = TWH_KEY_PAGE_DOWN;
    g_key_code_table[XK_End] = TWH_KEY_END;
    g_key_code_table[XK_Home] = TWH_KEY_HOME;
    g_key_code_table[XK_Insert] = TWH_KEY_INSERT;
    g_key_code_table[XK_Delete] = TWH_KEY_DELETE;
    g_key_code_table[XK_Up] = TWH_KEY_UP;
    g_key_code_table[XK_Down] = TWH_KEY_DOWN;
    g_key_code_table[XK_Left] = TWH_KEY_LEFT;
    g_key_code_table[XK_Right] = TWH_KEY_RIGHT;
    g_key_code_table[XK_Num_Lock] = TWH_KEY_NUM_LOCK;
    g_key_code_table[XK_Control_L] = TWH_KEY_CONTROL; /* TODO: Not working */
    g_key_code_table[XK_Control_R] = TWH_KEY_CONTROL;
    g_key_code_table[XK_Shift_L] = TWH_KEY_SHIFT; /* TODO: Not working */
    g_key_code_table[XK_Shift_R] = TWH_KEY_SHIFT;
    g_key_code_table[XK_Alt_L] = TWH_KEY_ALT; /* TODO: Not working */
    g_key_code_table[XK_Shift_L] = TWH_KEY_ALT;
    g_key_code_table[XK_KP_Add] = TWH_KEY_NUMPAD_ADD;
    g_key_code_table[XK_KP_Subtract] = TWH_KEY_NUMPAD_SUBTRACT;
    g_key_code_table[XK_KP_Multiply] = TWH_KEY_NUMPAD_MULTIPLY;
    g_key_code_table[XK_KP_Divide] = TWH_KEY_NUMPAD_DIVIDE;
    g_key_code_table[XK_KP_Decimal] = TWH_KEY_NUMPAD_DECIMAL;
    g_key_code_table[XK_semicolon] = TWH_KEY_SEMICOLON;
    g_key_code_table[XK_slash] = TWH_KEY_SLASH;
    g_key_code_table[XK_grave] = TWH_KEY_GRAVE_ACCENT;
    g_key_code_table[XK_braceleft] = TWH_KEY_LEFT_BRACKET;
    g_key_code_table[XK_backslash] = TWH_KEY_BACKSLASH;
    g_key_code_table[XK_braceright] = TWH_KEY_RIGHT_BRACKET;
    g_key_code_table[XK_apostrophe] = TWH_KEY_APOSTROPHE;
    g_key_code_table[XK_comma] = TWH_KEY_COMMA;
    g_key_code_table[XK_minus] = TWH_KEY_MINUS;
    g_key_code_table[XK_period] = TWH_KEY_PERIOD;
    g_key_code_table[XK_equal] = TWH_KEY_EQUAL;
}

static void create_surface(int width, int height, unsigned char **out_surface, XImage **out_ximage)
{
    int screen = XDefaultScreen(g_display);
    int depth = XDefaultDepth(g_display, screen);
    Visual *visual = XDefaultVisual(g_display, screen);
    XImage *ximage;

    assert(depth == 24 || depth == 32);
    unsigned char *surface = (unsigned char *)malloc(width * height * SURFACE_CHANNELS);
    ximage = XCreateImage(g_display, visual, depth, ZPixmap, 0,
                          (char *)surface, width, height, 32, 0);

    *out_surface = surface;
    *out_ximage = ximage;
}

static void present_surface(twh_window_t *wnd)
{
    int screen = XDefaultScreen(g_display);
    GC gc = XDefaultGC(g_display, screen);
    XPutImage(g_display, wnd->handle, gc, wnd->ximage,
              0, 0, 0, 0, wnd->surface_w, wnd->surface_h);
    XFlush(g_display);
}

static void blit_bgr(unsigned char *src, int src_w, int src_h, unsigned char *dst, int dst_w, int dst_h)
{
    assert(src_w == dst_w && src_h == dst_h);

    int r, c;
    int width = dst_w;
    int height = dst_h;

    for (r = 0; r < height; r++)
    {
        for (c = 0; c < width; c++)
        {
            int flipped_r = height - 1 - r;
            int src_index = (r * width + c) * SURFACE_CHANNELS;
            int dst_index = (flipped_r * width + c) * SURFACE_CHANNELS;
            unsigned char *src_pixel = &src[src_index];
            unsigned char *dst_pixel = &dst[dst_index];
            dst_pixel[0] = src_pixel[2]; /* blue */
            dst_pixel[1] = src_pixel[1]; /* green */
            dst_pixel[2] = src_pixel[0]; /* red */
        }
    }
}

static TWH_KEY_CODE get_key_code(int virtual_key)
{
    return g_key_code_table[virtual_key];
}

static void handle_key_event(twh_window_t *wnd, int virtual_key, char pressed)
{
    KeySym *keysyms;
    KeySym keysym;
    TWH_KEY_CODE key;
    int dummy;

    keysyms = XGetKeyboardMapping(g_display, virtual_key, 1, &dummy);
    keysym = keysyms[0];
    XFree(keysyms);

    key = get_key_code(keysym);

    if (key < TWH_KEY_NUM)
    {
        if (wnd->key_callback)
        {
            wnd->key_callback(wnd, key, pressed);
        }
    }
}

static void handle_mouse_event(twh_window_t *wnd, int xbutton, char pressed)
{
    /* mouse button */
    if (xbutton == Button1 || xbutton == Button2 || xbutton == Button3)
    {
        TWH_MOUSE_BUTTON button;
        switch (xbutton)
        {
        case Button1:
            button = TWH_MOUSE_LEFT_BUTTON;
            break;
        case Button2:
            button = TWH_MOUSE_MIDDLE_BUTTON;
            break;
        case Button3:
            button = TWH_MOUSE_RIGHT_BUTTON;
            break;
        default:
            break;
        }

        if (button < TWH_MOUSE_BUTTON_NUM && wnd->mouse_callback)
        {
            wnd->mouse_callback(wnd, button, pressed);
        }
    }
    /* mouse wheel */
    else if (xbutton == Button4 || xbutton == Button5)
    {
        if (wnd->scroll_callback)
        {
            float offset = xbutton == Button4 ? 1 : -1;
            wnd->scroll_callback(wnd, offset);
        }
    }
}

static void handle_client_event(twh_window_t *wnd, XClientMessageEvent *event)
{
    static Atom protocols = None;
    static Atom delete_window = None;
    if (protocols == None)
    {
        protocols = XInternAtom(g_display, "WM_PROTOCOLS", True);
        delete_window = XInternAtom(g_display, "WM_DELETE_WINDOW", True);
        assert(protocols != None);
        assert(delete_window != None);
    }
    if (event->message_type == protocols)
    {
        Atom protocol = event->data.l[0];
        if (protocol == delete_window)
        {
            wnd->should_close = 1;
        }
    }
}

static void process_event(XEvent *event)
{
    Window handle;
    twh_window_t *window;
    int error;

    handle = event->xany.window;
    error = XFindContext(g_display, handle, g_context, (XPointer *)&window);
    if (error != 0)
    {
        return;
    }

    if (event->type == ClientMessage)
    {
        handle_client_event(window, &event->xclient);
    }
    else if (event->type == KeyPress)
    {
        handle_key_event(window, event->xkey.keycode, 1);
    }
    else if (event->type == KeyRelease)
    {
        handle_key_event(window, event->xkey.keycode, 0);
    }
    else if (event->type == ButtonPress)
    {
        handle_mouse_event(window, event->xbutton.button, 1);
    }
    else if (event->type == ButtonRelease)
    {
        handle_mouse_event(window, event->xbutton.button, 0);
    }
}