#include <kos.h>
#include <dc/biosfont.h>
#include <dc/maple/controller.h>
#include <kos/irq.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "history.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

enum {
    PLOT_X = 16,
    PLOT_Y = 16,
    PLOT_SIZE = 448,
    DOT_SIZE = 2,
    PANEL_X = 488,
    WHITE = 0xffff,
    DIM = 0x8410,
    GRID = 0x18e3,
    AXIS = 0x4208
};

static void rectangle(int x, int y, int width, int height, uint16_t color) {
    for(int row = y; row < y + height; ++row) {
        uint16_t *pixels = vram_s + row * vid_mode->width + x;
        for(int column = 0; column < width; ++column)
            pixels[column] = color;
    }
}

static int plot_coordinate(uint8_t value) {
    // map 00..FF to the full plot, leaving room for the last two-pixel dot
    const int last_dot = PLOT_SIZE - DOT_SIZE;
    return ((int)value * last_dot + 127) / 255;
}

static void text(int y, const char *string, uint16_t color) {
    bfont_draw_str_ex(vram_s + y * vid_mode->width + PANEL_X,
                     vid_mode->width, color, 0, 16, false, string);
}

static void draw_screen(bool connected, int port, uint8_t x, uint8_t y) {
    char line[24];
    vid_clear(0, 0, 0);

    // each grid line marks a raw Maple coordinate, with 80 as the center
    for(unsigned value = 0; value <= 255; value += 32) {
        const int offset = plot_coordinate((uint8_t)value);
        const uint16_t color = value == 128 ? AXIS : GRID;
        rectangle(PLOT_X + offset, PLOT_Y, 1, PLOT_SIZE, color);
        rectangle(PLOT_X, PLOT_Y + offset, PLOT_SIZE, 1, color);
    }
    rectangle(PLOT_X - 2, PLOT_Y - 2, PLOT_SIZE + 4, 1, DIM);
    rectangle(PLOT_X - 2, PLOT_Y + PLOT_SIZE + 1, PLOT_SIZE + 4, 1, DIM);
    rectangle(PLOT_X - 2, PLOT_Y - 2, 1, PLOT_SIZE + 4, DIM);
    rectangle(PLOT_X + PLOT_SIZE + 1, PLOT_Y - 2, 1, PLOT_SIZE + 4, DIM);

    // oldest first, so a newer visit to the same coordinate gets its own color
    const unsigned first = (stick_history.next + FT0_HISTORY_CAPACITY -
                            stick_history.count) % FT0_HISTORY_CAPACITY;
    for(unsigned i = 0; i < stick_history.count; ++i) {
        const stick_point_t *point = &stick_history.points[(first + i) % FT0_HISTORY_CAPACITY];
        const int dot_x = PLOT_X + plot_coordinate(point->x);
        const int dot_y = PLOT_Y + plot_coordinate(point->y);
        rectangle(dot_x, dot_y, DOT_SIZE, DOT_SIZE, point->color);
    }
    if(connected) {
        const int live_x = PLOT_X + plot_coordinate(x);
        const int live_y = PLOT_Y + plot_coordinate(y);
        rectangle(live_x, live_y, DOT_SIZE, DOT_SIZE, WHITE);
    }

    text(16, "FT0", WHITE);
    text(40, "Stick test", DIM);
    if(connected) {
        snprintf(line, sizeof(line), "Port %c", 'A' + port);
        text(80, line, DIM);
        snprintf(line, sizeof(line), "X: 0x%02X", (unsigned)x);
        text(112, line, WHITE);
        snprintf(line, sizeof(line), "Y: 0x%02X", (unsigned)y);
        text(140, line, WHITE);
    } else {
        text(80, "No pad", DIM);
        text(112, "X: --", DIM);
        text(140, "Y: --", DIM);
    }

    text(184, "Min / Max", DIM);
    if(stick_history.count) {
        snprintf(line, sizeof(line), "X %02X / %02X",
                 (unsigned)stick_history.min_x, (unsigned)stick_history.max_x);
        text(212, line, WHITE);
        snprintf(line, sizeof(line), "Y %02X / %02X",
                 (unsigned)stick_history.min_y, (unsigned)stick_history.max_y);
        text(240, line, WHITE);
    } else {
        text(212, "X -- / --", DIM);
        text(240, "Y -- / --", DIM);
    }
    snprintf(line, sizeof(line), "%u / 512", stick_history.count);
    text(280, line, WHITE);
    text(304, "Trail dots", DIM);
    for(int pixel = 0; pixel < 120; ++pixel) {
        const float distance = FT0_RED_DISTANCE * (float)pixel / 119.0f;
        rectangle(PANEL_X + pixel, 340, 1, 8, ft0_distance_color(distance));
    }
    text(352, "Step: 0..32", DIM);
    text(400, "X: clear", WHITE);
    text(432, "Start: exit", DIM);
}

int main(void) {
    uint32_t previous_buttons = 0;
    int previous_port = -1;
    bool was_connected = false;
    uint8_t x = 128, y = 128;

    vid_set_mode(DM_640x480 | DM_MULTIBUFFER, PM_RGB565);
    ft0_clear_history(x, y);

    while(true) {
        maple_device_t *controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        const cont_state_t *state = controller ? maple_dev_status(controller) : NULL;
        const bool connected = state != NULL;
        int port = -1;

        if(connected) {
            // copy X, Y and buttons together before a Maple IRQ updates the state
            cont_state_t sample;
            const irq_mask_t mask = irq_disable();
            memcpy(&sample, state, sizeof(sample));
            irq_restore(mask);
            port = controller->port;

            // KOS subtracts 128 from these bytes; undo that without rescaling or deadzones
            x = (uint8_t)(sample.joyx + 128);
            y = (uint8_t)(sample.joyy + 128);
            if(!was_connected || port != previous_port) {
                ft0_clear_history(x, y);
                previous_buttons = sample.buttons;
            }
            const uint32_t pressed = sample.buttons & ~previous_buttons;
            previous_buttons = sample.buttons;
            if(pressed & CONT_START) break;
            if(pressed & CONT_X)
                ft0_clear_history(x, y);
            else
                ft0_record_position(x, y);
        }
        was_connected = connected;
        previous_port = port;

        draw_screen(connected, port, x, y);
        vid_waitvbl();
        vid_flip(-1);
    }
    return 0;
}
