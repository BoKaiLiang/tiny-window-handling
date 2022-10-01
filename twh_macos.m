/*  TODO! Not done yet!!!  */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include <Cocoa/Cocoa.h>
#include <mach-o/dyld.h>
#include <mach/mach_time.h>
#include <unistd.h>

#include "twh.h"

#define SURFACE_CHANNELS 4

struct twh_window()
{
    NSWindow *handle;

    int surface_w;
    int surface_h;
    unsigned char *surface;

    int should_close;
    void *userdata;

    twh_key_callback_func_t key_callback;
    twh_mouse_callback_func_t mouse_callback;
    twh_scroll_callback_func_t scroll_callback;
}

static NSAutoreleasePool *g_autoreleasepool = NULL;
static int g_key_code_table[0xff] = {0};

/* declarations */

static void create_menu_bar(void);
static void create_application(void);
static double get_native_time(void);

static TWH_KEY_CODE get_key_code(int virtual_key);
static void handle_key_event(twh_window_t* wnd, int virtual_key, char pressed);
static void handle_mouse_event(twh_window_t* wnd, TWH_MOUSE_BUTTON mb, char pressed);
static void handle_scroll_event(twh_window_t* wnd, float offset);

static NSWindow create_macos_window(twh_window_t* wnd, const char* title, int width, int height);
static void create_key_code_table();

static void present_surface(twh_window_t *wnd);
static void blit_rgb(unsigned char *src, int src_w, int src_h, unsigned char *dst, int dst_w, int dst_h);

/* implementaions */

void twh_init(void)
{
    create_application();   
}

void twh_terminate(void)
{
    assert(g_autoreleasepool != NULL);
    [g_autoreleasepool drain];
    g_autoreleasepool = [[NSAutoreleasePool alloc] init];
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
    twh_window_t *window;

    assert(NSApp && width > 0 && height > 0);

    window = (twh_window_t*)malloc(sizeof(twh_window_t));
    memset(window, 0, sizeof(twh_window_t));
    window->handle = create_macos_window(window, title, width, height);
    window->surface = (unsigned char*)malloc(width * height * SURFACE_CHANNELS);
    window->width = width;
    window->height = height;

    [window->handle makeKeyAndOrderFront:nil];
    return window;
}

void twh_window_release(twh_window_t *wnd)
{
    [wnd->handle orderOut:nil];

    [[wnd->handle delegate] release];
    [wnd->handle close];

    [g_autoreleasepool drain];
    g_autoreleasepool = [[NSAutoreleasePool alloc] init];

    if (wnd->surface != NULL)
    {
        free(wnd->surface);
        wnd->surface = NULL;
    }
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
    while (1) {
        NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event == nil) {
            break;
        }
        [NSApp sendEvent:event];
    }
    [g_autoreleasepool drain];
    g_autoreleasepool = [[NSAutoreleasePool alloc] init];
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
    wnd->mouse_callback = scroll_callback;
}

void twh_get_cursor_pos(twh_window_t *wnd, float *xpos, float *ypos)
{
    NSPoint point = [wnd->handle mouseLocationOutsideOfEventStream];
    NSRect rect = [[wnd->handle contentView] frame];
    *xpos = (float)point.x;
    *ypos = (float)(rect.size.height - 1 - point.y); // flipped vertical
}

twh_framebuffer_t *twh_framebuffer_create(int width, int height)
{
    twh_framebuffer_t *framebuffer = (twh_framebuffer_t *)malloc(sizeof(twh_framebuffer_t));
    framebuffer->width = width;
    framebuffer->height = height;
    size_t sz = width * height * sizeof(unsigned char) * BITMAP_CHANNELS;
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
    int index = (y * fb->width + x) * BITMAP_CHANNELS;
    fb->buffer[index + 0] = r;
    fb->buffer[index + 1] = g;
    fb->buffer[index + 2] = b;
}

void twh_framebuffer_set_color_u32(twh_framebuffer_t *fb, int x, int y, uint32_t rgb)
{
    int index = (y * fb->width + x) * BITMAP_CHANNELS;
    fb->buffer[index + 0] = (rgb >> 16) & 0xff;
    fb->buffer[index + 1] = (rgb >> 8) & 0xff;
    fb->buffer[index + 2] = rgb & 0xff;
}

void twh_framebuffer_render(twh_window_t *wnd, twh_framebuffer_t *fb)
{
    blit_rgb(fb->buffer, fb->width, fb->height, wnd->bitmap, wnd->bitmap_w, wnd->bitmap_h);
    present_surface(wnd);
}

/* private functions */

static void create_menu_bar(void)
{
    NSMenu *menu_bar, *app_menu;
    NSMenuItem *app_menu_item, *quit_menu_item;
    NSString *app_name, *quit_title;

    menu_bar = [[[NSMenu alloc] init] autorelease];
    [NSApp setMainMenu:menu_bar];

    app_menu_item = [[[NSMenuItem alloc] init] autorelease];
    [menu_bar addItem:app_menu_item];

    app_menu = [[[NSMenu alloc] init] autorelease];
    [app_menu_item setSubmenu:app_menu];

    app_name = [[NSProcessInfo processInfo] processName];
    quit_title = [@"Quit " stringByAppendingString:app_name];
    quit_menu_item = [[[NSMenuItem alloc] initWithTitle:quit_title
                                                 action:@selector(terminate:)
                                          keyEquivalent:@"q"] autorelease];
    [app_menu addItem:quit_menu_item];
}

static void create_application(void)
{
    if (NSApp == nil) {
        g_autoreleasepool = [[NSAutoreleasePool alloc] init];
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        create_menubar();
        [NSApp finishLaunching];
    }
}

static double get_native_time(void)
{
    static double period = -1;
    if (period < 0) {
        mach_timebase_info_data_t info;
        mach_timebase_info(&info);
        period = (double)info.numer / (double)info.denom / 1e9;
    }
    return mach_absolute_time() * period;
}

@interface WindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation WindowDelegate {
    window_t *_window;
}

- (instancetype)initWithWindow:(window_t *)window {
    self = [super init];
    if (self != nil) {
        _window = window;
    }
    return self;
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    UNUSED_VAR(sender);
    _window->should_close = 1;
    return NO;
}

@end

static TWH_KEY_CODE get_key_code(int virtual_key)
{
    return g_key_code_table[virtual_key];
}

static void handle_key_event(twh_window_t* wnd, int virtual_key, char pressed)
{
    if (key < KEY_NUM) {
        if (wnd->key_callback) {
            wnd->key_callback(wnd, key, pressed);
        }
    }
}

static void handle_mouse_event(twh_window_t* wnd, TWH_MOUSE_BUTTON mb, char pressed)
{
    if (wnd->button_callback) {
        wnd->button_callback(wnd, mb, pressed);
    }
}

static void handle_scroll_event(twh_window_t* wnd, float offset)
{
    if (wnd->scroll_callback) {
        wnd->scroll_callback(wnd, offset);
    }
}

@interface ContentView : NSView
@end

@implementation ContentView {
    twh_window_t *_window;
}

- (instancetype)initWithWindow:(twh_window_t *)window {
    self = [super init];
    if (self != nil) {
        _window = window;
    }
    return self;
}

- (BOOL)acceptsFirstResponder {
    return YES;  /* to receive key-down events */
}

- (void)drawRect:(NSRect)dirtyRect {
    unsigned char *surface = _window->surface;
    int surface_w = _window0>surface_w;
    int surface_h = _window0>surface_h;
    NSBitmapImageRep *rep = [[[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:&(surface)
                          pixelsWide:surface_w
                          pixelsHigh:surface_h
                       bitsPerSample:8
                     samplesPerPixel:3
                            hasAlpha:NO
                            isPlanar:NO
                      colorSpaceName:NSCalibratedRGBColorSpace
                         bytesPerRow:surface_w * SURFACE_CHANNELS;
                        bitsPerPixel:32] autorelease];
    NSImage *nsimage = [[[NSImage alloc] init] autorelease];
    [nsimage addRepresentation:rep];
    [nsimage drawInRect:dirtyRect];
}

- (void)keyDown:(NSEvent *)event {
    handle_key_event(_window, [event keyCode], 1);
}

- (void)keyUp:(NSEvent *)event {
    handle_key_event(_window, [event keyCode], 0);
}

- (void)mouseDown:(NSEvent *)event {
    UNUSED_VAR(event);
    handle_button_event(_window, BUTTON_L, 1);
}

- (void)mouseUp:(NSEvent *)event {
    UNUSED_VAR(event);
    handle_button_event(_window, BUTTON_L, 0);
}

- (void)rightMouseDown:(NSEvent *)event {
    UNUSED_VAR(event);
    handle_button_event(_window, BUTTON_R, 1);
}

- (void)rightMouseUp:(NSEvent *)event {
    UNUSED_VAR(event);
    handle_button_event(_window, BUTTON_R, 0);
}

- (void)scrollWheel:(NSEvent *)event {
    float offset = (float)[event scrollingDeltaY];
    if ([event hasPreciseScrollingDeltas]) {
        offset *= 0.1f;
    }
    handle_scroll_event(_window, offset);
}

@end

static NSWindow create_maxos_window(twh_window_t* wnd, const char* title, int width, int height)
{
    NSRect rect;
    NSUInteger mask;
    NSWindow *handle;
    WindowDelegate *delegate;
    ContentView *view;

    rect = NSMakeRect(0, 0, width, height);
    mask = NSWindowStyleMaskTitled
           | NSWindowStyleMaskClosable
           | NSWindowStyleMaskMiniaturizable;
    handle = [[NSWindow alloc] initWithContentRect:rect
                                         styleMask:mask
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    assert(handle != nil);
    [handle setTitle:[NSString stringWithUTF8String:title]];
    [handle setColorSpace:[NSColorSpace genericRGBColorSpace]];

    /*
     * the storage semantics of NSWindow.setDelegate is @property(assign),
     * or @property(weak) with ARC, we must not autorelease the delegate
     */
    delegate = [[WindowDelegate alloc] initWithWindow:window];
    assert(delegate != nil);
    [handle setDelegate:delegate];

    view = [[[ContentView alloc] initWithWindow:window] autorelease];
    assert(view != nil);
    [handle setContentView:view];
    [handle makeFirstResponder:view];

    return handle;
}

static void create_key_code_table()
{
    /* TODO: Need mac to test */

    /* initialize */
    memset(g_key_code_table, TWH_KEY_NUM, 0xff * sizeof(int));

    /* Number 1 ~ 9 */
    g_key_code_table[0x1D] = TWH_KEY_0;
    g_key_code_table[0x12] = TWH_KEY_1;
    g_key_code_table[0x13] = TWH_KEY_2;
    g_key_code_table[0x14] = TWH_KEY_3;
    g_key_code_table[0x15] = TWH_KEY_4;
    g_key_code_table[0x17] = TWH_KEY_5;
    g_key_code_table[0x16] = TWH_KEY_6;
    g_key_code_table[0x1A] = TWH_KEY_7;
    g_key_code_table[0x1C] = TWH_KEY_8;
    g_key_code_table[0x19] = TWH_KEY_9;

    /* Alphabet */
    g_key_code_table[0x00] = TWH_KEY_A;
    g_key_code_table[0x0B] = TWH_KEY_B;
    g_key_code_table[0x08] = TWH_KEY_C;
    g_key_code_table[0x02] = TWH_KEY_D;
    g_key_code_table[0x0E] = TWH_KEY_E;
    g_key_code_table[0x03] = TWH_KEY_F;
    g_key_code_table[0x05] = TWH_KEY_G;
    g_key_code_table[0x04] = TWH_KEY_H;
    g_key_code_table[0x22] = TWH_KEY_I;
    g_key_code_table[0x26] = TWH_KEY_J;
    g_key_code_table[0x28] = TWH_KEY_K;
    g_key_code_table[0x25] = TWH_KEY_L;
    g_key_code_table[0x2E] = TWH_KEY_M;
    g_key_code_table[0x2F] = TWH_KEY_N;
    g_key_code_table[0x1F] = TWH_KEY_O;
    g_key_code_table[0x20] = TWH_KEY_P;
    g_key_code_table[0x0C] = TWH_KEY_Q;
    g_key_code_table[0x0F] = TWH_KEY_R;
    g_key_code_table[0x01] = TWH_KEY_S;
    g_key_code_table[0x11] = TWH_KEY_T;
    g_key_code_table[0x20] = TWH_KEY_U;
    g_key_code_table[0x09] = TWH_KEY_V;
    g_key_code_table[0x0D] = TWH_KEY_W;
    g_key_code_table[0x07] = TWH_KEY_X;
    g_key_code_table[0x10] = TWH_KEY_Y;
    g_key_code_table[0x06] = TWH_KEY_Z;

    /* Num pad numbers */
    g_key_code_table[0x52] = TWH_KEY_NUMPAD_0;
    g_key_code_table[0x53] = TWH_KEY_NUMPAD_1;
    g_key_code_table[0x54] = TWH_KEY_NUMPAD_2;
    g_key_code_table[0x55] = TWH_KEY_NUMPAD_3;
    g_key_code_table[0x56] = TWH_KEY_NUMPAD_4;
    g_key_code_table[0x57] = TWH_KEY_NUMPAD_5;
    g_key_code_table[0x58] = TWH_KEY_NUMPAD_6;
    g_key_code_table[0x59] = TWH_KEY_NUMPAD_7;
    g_key_code_table[0x5B] = TWH_KEY_NUMPAD_8;
    g_key_code_table[0x5C] = TWH_KEY_NUMPAD_9;

    /* F1 ~ F24 */
    g_key_code_table[0x7A] = TWH_KEY_F1;
    g_key_code_table[0x78] = TWH_KEY_F2;
    g_key_code_table[0x63] = TWH_KEY_F3;
    g_key_code_table[0x76] = TWH_KEY_F4;
    g_key_code_table[0x60] = TWH_KEY_F5;
    g_key_code_table[0x61] = TWH_KEY_F6;
    g_key_code_table[0x62] = TWH_KEY_F7;
    g_key_code_table[0x64] = TWH_KEY_F8;
    g_key_code_table[0x65] = TWH_KEY_F9;
    g_key_code_table[0x6D] = TWH_KEY_F10;
    g_key_code_table[0x67] = TWH_KEY_F11;
    g_key_code_table[0x6F] = TWH_KEY_F12;
    g_key_code_table[0x69] = TWH_KEY_F13;
    g_key_code_table[0x6B] = TWH_KEY_F14;
    g_key_code_table[0x71] = TWH_KEY_F15;
    g_key_code_table[0x6A] = TWH_KEY_F16;
    g_key_code_table[0x40] = TWH_KEY_F17;
    g_key_code_table[0x4F] = TWH_KEY_F18;
    g_key_code_table[0x50] = TWH_KEY_F19;
    g_key_code_table[0x5A] = TWH_KEY_F20;

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
    g_key_code_table[XK_Control_L] = TWH_KEY_CONTROL_L;
    g_key_code_table[XK_Control_R] = TWH_KEY_CONTROL_R;
    g_key_code_table[XK_Shift_L] = TWH_KEY_SHIFT_L;
    g_key_code_table[XK_Shift_R] = TWH_KEY_SHIFT_R;
    g_key_code_table[XK_Alt_L] = TWH_KEY_ALT_L;
    g_key_code_table[XK_Shift_L] = TWH_KEY_ALT_R;
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
    g_key_code_table[0x18] = TWH_KEY_EQUAL;
}

static void present_surface(twh_window_t *wnd)
{
    [[wnd->handle contentView] setNeedsDisplay:YES];  /* invoke drawRect */
}

static void blit_rgb(unsigned char *src, int src_w, int src_h, unsigned char *dst, int dst_w, int dst_h)
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
            int src_index = (r * width + c) * BITMAP_CHANNELS;
            int dst_index = (flipped_r * width + c) * BITMAP_CHANNELS;
            unsigned char *src_pixel = &src[src_index];
            unsigned char *dst_pixel = &dst[dst_index];
            dst_pixel[0] = src_pixel[0]; /* red */
            dst_pixel[1] = src_pixel[1]; /* green */
            dst_pixel[2] = src_pixel[2]; /* blue */
        }
    }
}

