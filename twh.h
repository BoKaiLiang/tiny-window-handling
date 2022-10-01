#ifndef TWH_H
#define TWH_H

#include <stdint.h>

typedef struct twh_window twh_window_t;

typedef struct twh_framebuffer
{
    int width, height;
    unsigned char *buffer;
} twh_framebuffer_t;

enum TWH_KEY_CODE
{
    /* Printable keys */
    TWH_KEY_SPACE,
    TWH_KEY_APOSTROPHE, /* ' */
    TWH_KEY_COMMA,      /* , */
    TWH_KEY_MINUS,      /* - */
    TWH_KEY_PERIOD,     /* . */
    TWH_KEY_SLASH,      /* / */
    TWH_KEY_0,
    TWH_KEY_1,
    TWH_KEY_2,
    TWH_KEY_3,
    TWH_KEY_4,
    TWH_KEY_5,
    TWH_KEY_6,
    TWH_KEY_7,
    TWH_KEY_8,
    TWH_KEY_9,
    TWH_KEY_SEMICOLON, /* ; */
    TWH_KEY_EQUAL,     /* = */
    TWH_KEY_A,
    TWH_KEY_B,
    TWH_KEY_C,
    TWH_KEY_D,
    TWH_KEY_E,
    TWH_KEY_F,
    TWH_KEY_G,
    TWH_KEY_H,
    TWH_KEY_I,
    TWH_KEY_J,
    TWH_KEY_K,
    TWH_KEY_L,
    TWH_KEY_M,
    TWH_KEY_N,
    TWH_KEY_O,
    TWH_KEY_P,
    TWH_KEY_Q,
    TWH_KEY_R,
    TWH_KEY_S,
    TWH_KEY_T,
    TWH_KEY_U,
    TWH_KEY_V,
    TWH_KEY_W,
    TWH_KEY_X,
    TWH_KEY_Y,
    TWH_KEY_Z,
    TWH_KEY_LEFT_BRACKET,  /* [ */
    TWH_KEY_BACKSLASH,     /* \ */
    TWH_KEY_RIGHT_BRACKET, /* ] */
    TWH_KEY_GRAVE_ACCENT,  /* ` */

    /* Function keys */
    TWH_KEY_ESCAPE,
    TWH_KEY_ENTER,
    TWH_KEY_TAB,
    TWH_KEY_BACKSPACE,
    TWH_KEY_INSERT,
    TWH_KEY_DELETE,
    TWH_KEY_RIGHT,
    TWH_KEY_LEFT,
    TWH_KEY_DOWN,
    TWH_KEY_UP,
    TWH_KEY_PAGE_UP,
    TWH_KEY_PAGE_DOWN,
    TWH_KEY_HOME,
    TWH_KEY_END,
    TWH_KEY_CAPS_LOCK,
    TWH_KEY_SCROLL_LOCK,
    TWH_KEY_NUM_LOCK,
    TWH_KEY_PRINT_SCREEN,
    TWH_KEY_PAUSE,
    TWH_KEY_F1,
    TWH_KEY_F2,
    TWH_KEY_F3,
    TWH_KEY_F4,
    TWH_KEY_F5,
    TWH_KEY_F6,
    TWH_KEY_F7,
    TWH_KEY_F8,
    TWH_KEY_F9,
    TWH_KEY_F10,
    TWH_KEY_F11,
    TWH_KEY_F12,
    TWH_KEY_F13,
    TWH_KEY_F14,
    TWH_KEY_F15,
    TWH_KEY_F16,
    TWH_KEY_F17,
    TWH_KEY_F18,
    TWH_KEY_F19,
    TWH_KEY_F20,
    TWH_KEY_F21,
    TWH_KEY_F22,
    TWH_KEY_F23,
    TWH_KEY_F24,
    TWH_KEY_F25,
    TWH_KEY_NUMPAD_0,
    TWH_KEY_NUMPAD_1,
    TWH_KEY_NUMPAD_2,
    TWH_KEY_NUMPAD_3,
    TWH_KEY_NUMPAD_4,
    TWH_KEY_NUMPAD_5,
    TWH_KEY_NUMPAD_6,
    TWH_KEY_NUMPAD_7,
    TWH_KEY_NUMPAD_8,
    TWH_KEY_NUMPAD_9,
    TWH_KEY_NUMPAD_DECIMAL,
    TWH_KEY_NUMPAD_DIVIDE,
    TWH_KEY_NUMPAD_MULTIPLY,
    TWH_KEY_NUMPAD_SUBTRACT,
    TWH_KEY_NUMPAD_ADD,
    TWH_KEY_NUMPAD_ENTER,
    TWH_KEY_NUMPAD_EQUAL,
    TWH_KEY_SHIFT,
    TWH_KEY_CONTROL,
    TWH_KEY_ALT,

    TWH_KEY_NUM,
}; /* Functions key */
typedef enum TWH_KEY_CODE TWH_KEY_CODE;

enum TWH_MOUSE_BUTTON
{
    TWH_MOUSE_LEFT_BUTTON = 0,
    TWH_MOUSE_RIGHT_BUTTON = 1,
    TWH_MOUSE_MIDDLE_BUTTON = 2,

    TWH_MOUSE_BUTTON_NUM
};
typedef enum TWH_MOUSE_BUTTON TWH_MOUSE_BUTTON;

typedef void (*twh_key_callback_func_t)(twh_window_t *wnd, TWH_KEY_CODE keycode, int pressed);
typedef void (*twh_mouse_callback_func_t)(twh_window_t *wnd, TWH_MOUSE_BUTTON mb, int pressed);
typedef void (*twh_scroll_callback_func_t)(twh_window_t *wnd, float offset);

void twh_init(void);
void twh_terminate(void);
float twh_get_timef(void);

twh_window_t *twh_window_create(const char *title, int width, int height);
void twh_window_release(twh_window_t *wnd);
void twh_set_user_data(twh_window_t *wnd, void *userdata);
void *twh_get_user_data(twh_window_t *wnd);
int twh_window_should_close(twh_window_t *wnd);
void twh_window_close(twh_window_t *wnd);

void twh_poll_events();
void twh_set_key_callback(twh_window_t *wnd, twh_key_callback_func_t key_callback);
void twh_set_mouse_callback(twh_window_t *wnd, twh_mouse_callback_func_t mouse_callback);
void twh_set_scroll_callback(twh_window_t *wnd, twh_scroll_callback_func_t scroll_callback);
void twh_get_cursor_pos(twh_window_t *wnd, float *x, float *y);

twh_framebuffer_t *twh_framebuffer_create(int width, int height);
void twh_framebuffer_release(twh_framebuffer_t *fb);
void twh_framebuffer_set_color_u8(twh_framebuffer_t *fb, int x, int y, uint8_t r, uint8_t g, uint8_t b);
void twh_framebuffer_set_color_u32(twh_framebuffer_t *fb, int x, int y, uint32_t rgb);
void twh_framebuffer_render(twh_window_t *wnd, twh_framebuffer_t *fb);

#endif /* TWH_H */