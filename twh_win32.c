#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include <windows.h>
#include <windowsx.h>
#include <direct.h>

#include "twh.h"

#define BITMAP_CHANNELS 4

struct twh_window
{
    HWND handle;
    HDC memory_dc;

    int bitmap_w;
    int bitmap_h;
    unsigned char *bitmap;

    int should_close;
    void *user_data;

    twh_key_callback_func_t key_callback;
    twh_mouse_callback_func_t mouse_callback;
    twh_scroll_callback_func_t scroll_callback;
};

static int g_initialized = 0;
static int g_key_code_table[0xff] = {0};

#ifdef UNICODE
static const wchar_t *const WINDOW_CLASS_NAME = L"Class";
static const wchar_t *const WINDOW_ENTRY_NAME = L"Entry";
#else
static const char *const WINDOW_CLASS_NAME = "Class";
static const char *const WINDOW_ENTRY_NAME = "Entry";
#endif

/* declarations */
static TWH_KEY_CODE get_key_code(int virtual_key);
static void handle_key_message(twh_window_t *wnd, WPARAM virtual_key, char pressed);
static void handle_mouse_button_message(twh_window_t *wnd, TWH_MOUSE_BUTTON mb, char pressed);
static void handle_mouse_scroll_message(twh_window_t *wnd, float offset);
static LRESULT process_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static void register_class(void);
static void unregister_class(void);
static double get_native_time(void);

static HWND create_win32_window(const char *title, int width, int height);
static void create_key_code_table(void);
static void create_bitmap(HWND handle, int width, int height, unsigned char **out_surface, HDC *out_memory_dc);
static void present_surface(twh_window_t *wnd);
static void blit_bgr(unsigned char *src, int src_w, int src_h, unsigned char *dst, int dst_w, int dst_h);

/* implmentations */
void twh_init()
{
    assert(g_initialized == 0);
    register_class();
    // initialize_path();
    g_initialized = 1;
}

void twh_terminate()
{
    assert(g_initialized == 1);
    unregister_class();
    g_initialized = 0;
}

float twh_get_timef()
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
    HWND handle;
    unsigned char *surface;
    HDC memory_dc;

    assert(g_initialized && width > 0 && height > 0);

    handle = create_win32_window(title, width, height);
    create_bitmap(handle, width, height, &surface, &memory_dc);
    create_key_code_table();

    window = (twh_window_t *)malloc(sizeof(twh_window_t));
    memset(window, 0, sizeof(twh_window_t));
    window->handle = handle;
    window->memory_dc = memory_dc;
    window->bitmap_w = width;
    window->bitmap_h = height;
    window->bitmap = surface;

    SetProp(handle, WINDOW_ENTRY_NAME, window);
    ShowWindow(handle, SW_SHOW);
    return window;
}

void twh_window_release(twh_window_t *wnd)
{
    ShowWindow(wnd->handle, SW_HIDE);
    RemoveProp(wnd->handle, WINDOW_ENTRY_NAME);

    DeleteDC(wnd->memory_dc);
    DestroyWindow(wnd->handle);

    if (wnd->bitmap)
    {
        free(wnd->bitmap);
        wnd->bitmap = NULL;
    }
    free(wnd);
    wnd = NULL;
}

void twh_set_user_data(twh_window_t *wnd, void *userdata)
{
    wnd->user_data = userdata;
}

void *twh_get_user_data(twh_window_t *wnd)
{
    return wnd->user_data;
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
    MSG message;
    while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
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
    POINT point;
    GetCursorPos(&point);
    ScreenToClient(wnd->handle, &point);
    *xpos = (float)point.x;
    *ypos = (float)point.y;
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
    blit_bgr(fb->buffer, fb->width, fb->height, wnd->bitmap, wnd->bitmap_w, wnd->bitmap_h);
    present_surface(wnd);
}

static TWH_KEY_CODE get_key_code(int virtual_key)
{
    return g_key_code_table[virtual_key];
}

static void handle_key_message(twh_window_t *wnd, WPARAM virtual_key, char pressed)
{
    TWH_KEY_CODE key = get_key_code(virtual_key);

    if (key < TWH_KEY_NUM)
    {
        if (wnd->key_callback != NULL)
        {
            wnd->key_callback(wnd, key, pressed);
        }
    }
}

static void handle_mouse_button_message(twh_window_t *wnd, TWH_MOUSE_BUTTON mb, char pressed)
{
    if (wnd->mouse_callback)
    {
        wnd->mouse_callback(wnd, mb, pressed);
    }
}

static void handle_mouse_scroll_message(twh_window_t *wnd, float offset)
{
    if (wnd->scroll_callback != NULL)
    {
        wnd->scroll_callback(wnd, offset);
    }
}

static LRESULT process_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    twh_window_t *window = (twh_window_t *)GetProp(hWnd, WINDOW_ENTRY_NAME);
    if (window == NULL)
    {
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    else if (uMsg == WM_CLOSE)
    {
        window->should_close = 1;
        return 0;
    }
    else if (uMsg == WM_KEYDOWN)
    {
        handle_key_message(window, wParam, 1);
        return 0;
    }
    else if (uMsg == WM_KEYUP)
    {
        handle_key_message(window, wParam, 0);
        return 0;
    }
    else if (uMsg == WM_LBUTTONDOWN)
    {
        handle_mouse_button_message(window, TWH_MOUSE_LEFT_BUTTON, 1);
        return 0;
    }
    else if (uMsg == WM_RBUTTONDOWN)
    {
        handle_mouse_button_message(window, TWH_MOUSE_RIGHT_BUTTON, 1);
        return 0;
    }
    else if (uMsg == WM_MBUTTONDOWN)
    {
        handle_mouse_button_message(window, TWH_MOUSE_MIDDLE_BUTTON, 1);
        return 0;
    }
    else if (uMsg == WM_LBUTTONUP)
    {
        handle_mouse_button_message(window, TWH_MOUSE_LEFT_BUTTON, 0);
        return 0;
    }
    else if (uMsg == WM_RBUTTONUP)
    {
        handle_mouse_button_message(window, TWH_MOUSE_RIGHT_BUTTON, 0);
        return 0;
    }
    else if (uMsg == WM_MBUTTONUP)
    {
        handle_mouse_button_message(window, TWH_MOUSE_MIDDLE_BUTTON, 0);
        return 0;
    }
    else if (uMsg == WM_MOUSEWHEEL)
    {
        float offset = GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
        handle_mouse_scroll_message(window, offset);
        return 0;
    }
    else
    {
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
}

static void register_class(void)
{
    ATOM class_atom;
    WNDCLASS window_class;
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = process_message;
    window_class.cbClsExtra = 0;
    window_class.cbWndExtra = 0;
    window_class.hInstance = GetModuleHandle(NULL);
    window_class.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    window_class.lpszMenuName = NULL;
    window_class.lpszClassName = WINDOW_CLASS_NAME;
    class_atom = RegisterClass(&window_class);
    assert(class_atom != 0);
}

static void unregister_class(void)
{
    UnregisterClass(WINDOW_CLASS_NAME, GetModuleHandle(NULL));
}

static double get_native_time(void)
{
    static double period = -1;
    LARGE_INTEGER counter;
    if (period < 0)
    {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        period = 1.0 / (double)frequency.QuadPart;
    }
    QueryPerformanceCounter(&counter);
    return counter.QuadPart * period;
}

static HWND create_win32_window(const char *title, int width, int height)
{
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect;
    HWND handle;

#ifdef UNICODE
    wchar_t dst_title[LINE_SIZE];
    mbstowcs(dst_title, title, LINE_SIZE);
#else
    const char *dst_title = title;
#endif

    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    AdjustWindowRect(&rect, style, 0);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;

    handle = CreateWindow(WINDOW_CLASS_NAME, dst_title, style,
                          CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                          NULL, NULL, GetModuleHandle(NULL), NULL);
    assert(handle != NULL);
    return handle;
}

static void create_key_code_table(void)
{
    /* initialize */
    memset(g_key_code_table, TWH_KEY_NUM, 0xff * sizeof(int));

    /* Number 1 ~ 9 */
    g_key_code_table[0x30] = TWH_KEY_0;
    g_key_code_table[0x31] = TWH_KEY_1;
    g_key_code_table[0x32] = TWH_KEY_2;
    g_key_code_table[0x33] = TWH_KEY_3;
    g_key_code_table[0x34] = TWH_KEY_4;
    g_key_code_table[0x35] = TWH_KEY_5;
    g_key_code_table[0x36] = TWH_KEY_6;
    g_key_code_table[0x37] = TWH_KEY_7;
    g_key_code_table[0x38] = TWH_KEY_8;
    g_key_code_table[0x39] = TWH_KEY_9;

    /* Alphabet */
    g_key_code_table[0x41] = TWH_KEY_A;
    g_key_code_table[0x42] = TWH_KEY_B;
    g_key_code_table[0x43] = TWH_KEY_C;
    g_key_code_table[0x44] = TWH_KEY_D;
    g_key_code_table[0x45] = TWH_KEY_E;
    g_key_code_table[0x46] = TWH_KEY_F;
    g_key_code_table[0x47] = TWH_KEY_G;
    g_key_code_table[0x48] = TWH_KEY_H;
    g_key_code_table[0x49] = TWH_KEY_I;
    g_key_code_table[0x4A] = TWH_KEY_J;
    g_key_code_table[0x4B] = TWH_KEY_K;
    g_key_code_table[0x4C] = TWH_KEY_L;
    g_key_code_table[0x4D] = TWH_KEY_M;
    g_key_code_table[0x4E] = TWH_KEY_N;
    g_key_code_table[0x4F] = TWH_KEY_O;
    g_key_code_table[0x50] = TWH_KEY_P;
    g_key_code_table[0x51] = TWH_KEY_Q;
    g_key_code_table[0x52] = TWH_KEY_R;
    g_key_code_table[0x53] = TWH_KEY_S;
    g_key_code_table[0x54] = TWH_KEY_T;
    g_key_code_table[0x55] = TWH_KEY_U;
    g_key_code_table[0x56] = TWH_KEY_V;
    g_key_code_table[0x57] = TWH_KEY_W;
    g_key_code_table[0x58] = TWH_KEY_X;
    g_key_code_table[0x59] = TWH_KEY_Y;
    g_key_code_table[0x5A] = TWH_KEY_Z;

    /* Num pad numbers */
    g_key_code_table[0x60] = TWH_KEY_NUMPAD_0;
    g_key_code_table[0x61] = TWH_KEY_NUMPAD_1;
    g_key_code_table[0x62] = TWH_KEY_NUMPAD_2;
    g_key_code_table[0x63] = TWH_KEY_NUMPAD_3;
    g_key_code_table[0x64] = TWH_KEY_NUMPAD_4;
    g_key_code_table[0x65] = TWH_KEY_NUMPAD_5;
    g_key_code_table[0x66] = TWH_KEY_NUMPAD_6;
    g_key_code_table[0x67] = TWH_KEY_NUMPAD_7;
    g_key_code_table[0x68] = TWH_KEY_NUMPAD_8;
    g_key_code_table[0x69] = TWH_KEY_NUMPAD_9;

    /* F1 ~ F24 */
    g_key_code_table[0x70] = TWH_KEY_F1;
    g_key_code_table[0x71] = TWH_KEY_F2;
    g_key_code_table[0x72] = TWH_KEY_F3;
    g_key_code_table[0x73] = TWH_KEY_F4;
    g_key_code_table[0x74] = TWH_KEY_F5;
    g_key_code_table[0x75] = TWH_KEY_F6;
    g_key_code_table[0x76] = TWH_KEY_F7;
    g_key_code_table[0x77] = TWH_KEY_F8;
    g_key_code_table[0x78] = TWH_KEY_F9;
    g_key_code_table[0x79] = TWH_KEY_F10;
    g_key_code_table[0x7A] = TWH_KEY_F11;
    g_key_code_table[0x7B] = TWH_KEY_F12;
    g_key_code_table[0x7C] = TWH_KEY_F13;
    g_key_code_table[0x7D] = TWH_KEY_F14;
    g_key_code_table[0x7E] = TWH_KEY_F15;
    g_key_code_table[0x7F] = TWH_KEY_F16;
    g_key_code_table[0x80] = TWH_KEY_F17;
    g_key_code_table[0x81] = TWH_KEY_F18;
    g_key_code_table[0x82] = TWH_KEY_F19;
    g_key_code_table[0x83] = TWH_KEY_F20;
    g_key_code_table[0x84] = TWH_KEY_F21;
    g_key_code_table[0x85] = TWH_KEY_F22;
    g_key_code_table[0x86] = TWH_KEY_F23;
    g_key_code_table[0x87] = TWH_KEY_F24;

    /* Functions */

    g_key_code_table[VK_BACK] = TWH_KEY_BACKSPACE;
    g_key_code_table[VK_TAB] = TWH_KEY_TAB;
    g_key_code_table[VK_RETURN] = TWH_KEY_ENTER;
    g_key_code_table[VK_PAUSE] = TWH_KEY_PAUSE;
    g_key_code_table[VK_SCROLL] = TWH_KEY_SCROLL_LOCK;
    g_key_code_table[VK_SNAPSHOT] = TWH_KEY_PRINT_SCREEN;
    g_key_code_table[VK_CAPITAL] = TWH_KEY_CAPS_LOCK;
    g_key_code_table[VK_ESCAPE] = TWH_KEY_ESCAPE;
    g_key_code_table[VK_SPACE] = TWH_KEY_SPACE;
    g_key_code_table[VK_PRIOR] = TWH_KEY_PAGE_UP;
    g_key_code_table[VK_NEXT] = TWH_KEY_PAGE_DOWN;
    g_key_code_table[VK_END] = TWH_KEY_END;
    g_key_code_table[VK_HOME] = TWH_KEY_HOME;
    g_key_code_table[VK_INSERT] = TWH_KEY_INSERT;
    g_key_code_table[VK_DELETE] = TWH_KEY_DELETE;
    g_key_code_table[VK_UP] = TWH_KEY_UP;
    g_key_code_table[VK_DOWN] = TWH_KEY_DOWN;
    g_key_code_table[VK_LEFT] = TWH_KEY_LEFT;
    g_key_code_table[VK_RIGHT] = TWH_KEY_RIGHT;
    g_key_code_table[VK_NUMLOCK] = TWH_KEY_NUM_LOCK;
    g_key_code_table[VK_CONTROL] = TWH_KEY_CONTROL;
    g_key_code_table[VK_SHIFT] = TWH_KEY_SHIFT;
    g_key_code_table[VK_MENU] = TWH_KEY_ALT;
    g_key_code_table[VK_ADD] = TWH_KEY_NUMPAD_ADD;
    g_key_code_table[VK_SUBTRACT] = TWH_KEY_NUMPAD_SUBTRACT;
    g_key_code_table[VK_MULTIPLY] = TWH_KEY_NUMPAD_MULTIPLY;
    g_key_code_table[VK_DIVIDE] = TWH_KEY_NUMPAD_DIVIDE;
    g_key_code_table[VK_DECIMAL] = TWH_KEY_NUMPAD_DECIMAL;
    g_key_code_table[VK_OEM_1] = TWH_KEY_SEMICOLON;
    g_key_code_table[VK_OEM_2] = TWH_KEY_SLASH;
    g_key_code_table[VK_OEM_3] = TWH_KEY_GRAVE_ACCENT;
    g_key_code_table[VK_OEM_4] = TWH_KEY_LEFT_BRACKET;
    g_key_code_table[VK_OEM_5] = TWH_KEY_BACKSLASH;
    g_key_code_table[VK_OEM_6] = TWH_KEY_RIGHT_BRACKET;
    g_key_code_table[VK_OEM_7] = TWH_KEY_APOSTROPHE;
    g_key_code_table[VK_OEM_COMMA] = TWH_KEY_COMMA;
    g_key_code_table[VK_OEM_MINUS] = TWH_KEY_MINUS;
    g_key_code_table[VK_OEM_PERIOD] = TWH_KEY_PERIOD;
    g_key_code_table[VK_OEM_PLUS] = TWH_KEY_EQUAL;
}

static void create_bitmap(HWND handle, int width, int height, unsigned char **out_surface, HDC *out_memory_dc)
{
    BITMAPINFOHEADER bi_header;
    HBITMAP dib_bitmap;
    HBITMAP old_bitmap;
    HDC window_dc;
    HDC memory_dc;

    unsigned char *surface = (unsigned char *)malloc(width * height * BITMAP_CHANNELS * sizeof(unsigned char));
    free(surface);
    surface = NULL;

    window_dc = GetDC(handle);
    memory_dc = CreateCompatibleDC(window_dc);
    ReleaseDC(handle, window_dc);

    memset(&bi_header, 0, sizeof(BITMAPINFOHEADER));
    bi_header.biSize = sizeof(BITMAPINFOHEADER);
    bi_header.biWidth = width;
    bi_header.biHeight = -height; /* top-down */
    bi_header.biPlanes = 1;
    bi_header.biBitCount = 32;
    bi_header.biCompression = BI_RGB;
    dib_bitmap = CreateDIBSection(memory_dc, (BITMAPINFO *)&bi_header,
                                  DIB_RGB_COLORS, (void **)&surface,
                                  NULL, 0);
    assert(dib_bitmap != NULL);
    old_bitmap = (HBITMAP)SelectObject(memory_dc, dib_bitmap);
    DeleteObject(old_bitmap);

    *out_surface = surface;
    *out_memory_dc = memory_dc;
}

static void present_surface(twh_window_t *wnd)
{
    HDC window_dc = GetDC(wnd->handle);
    HDC memory_dc = wnd->memory_dc;
    BitBlt(window_dc, 0, 0, wnd->bitmap_w, wnd->bitmap_h, memory_dc, 0, 0, SRCCOPY);
    ReleaseDC(wnd->handle, window_dc);
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
            int src_index = (r * width + c) * BITMAP_CHANNELS;
            int dst_index = (flipped_r * width + c) * BITMAP_CHANNELS;
            unsigned char *src_pixel = &src[src_index];
            unsigned char *dst_pixel = &dst[dst_index];
            dst_pixel[0] = src_pixel[2]; /* blue */
            dst_pixel[1] = src_pixel[1]; /* green */
            dst_pixel[2] = src_pixel[0]; /* red */
        }
    }
}
