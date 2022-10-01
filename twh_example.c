#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "twh.h"

#define UNUSED_VAR(x) ((void)x)

#define WND_W 800
#define WND_H 600

static void key_callback(twh_window_t *wnd, TWH_KEY_CODE keycode, int pressed)
{
    if (pressed == 1 && keycode == TWH_KEY_ESCAPE)
    {
        twh_window_close(wnd);
    }

    UNUSED_VAR(wnd);
}

static void mouse_callback(twh_window_t *wnd, TWH_MOUSE_BUTTON mb, int pressed)
{
    UNUSED_VAR(wnd);
    if (pressed)
    {
        switch (mb)
        {
        case TWH_MOUSE_LEFT_BUTTON:
            printf("LEFT CLICK!\n");
            break;
        case TWH_MOUSE_RIGHT_BUTTON:
            printf("RIGHT CLICK!\n");
            break;
        case TWH_MOUSE_MIDDLE_BUTTON:
            printf("MIDDLE CLICK!\n");
            break;
        default:
        case TWH_MOUSE_BUTTON_NUM:
            break;
        }
    }
}

static void scroll_callback(twh_window_t *wnd, float offset)
{
    UNUSED_VAR(wnd);
    printf("Scroll: %f\n", offset);
}

int main(void)
{
    twh_init();

    twh_window_t *wnd = twh_window_create("Example", WND_W, WND_H);
    twh_set_key_callback(wnd, key_callback);
    twh_set_mouse_callback(wnd, mouse_callback);
    twh_set_scroll_callback(wnd, scroll_callback);

    twh_framebuffer_t *fb = twh_framebuffer_create(WND_W, WND_H);
    for (int r = 0; r < WND_H; r++)
    {
        for (int c = 0; c < WND_W; c++)
        {
            twh_framebuffer_set_color_u32(fb, c, r, 0xff8800);
        }
    }

    while (!twh_window_should_close(wnd))
    {
        twh_framebuffer_render(wnd, fb);

        twh_poll_events();
    }

    twh_framebuffer_release(fb);
    twh_window_release(wnd);
    twh_terminate();
    return 0;
}