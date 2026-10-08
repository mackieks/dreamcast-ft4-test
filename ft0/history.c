#include "history.h"
#include <math.h>

stick_history_t stick_history;

uint16_t ft0_distance_color(float distance) {
    // sweep HSV hue from green (120 degrees) through yellow to red (0 degrees)
    float fraction = distance / FT0_RED_DISTANCE;
    if(fraction < 0.0f) fraction = 0.0f;
    if(fraction > 1.0f) fraction = 1.0f;
    const float hue = 120.0f * (1.0f - fraction);
    const float red = hue < 60.0f ? 1.0f : (120.0f - hue) / 60.0f;
    const float green = hue < 60.0f ? hue / 60.0f : 1.0f;

    // RGB565 is the only quantization; there are no speed buckets
    const unsigned red_bits = (unsigned)(red * 31.0f + 0.5f);
    const unsigned green_bits = (unsigned)(green * 63.0f + 0.5f);
    return (uint16_t)((red_bits << 11) | (green_bits << 5));
}

void ft0_clear_history(uint8_t x, uint8_t y) {
    stick_history.next = 0;
    stick_history.count = 0;
    stick_history.min_x = stick_history.max_x = x;
    stick_history.min_y = stick_history.max_y = y;

    // keep the current coordinate as our baseline so a stationary stick stays cleared
    stick_history.previous_x = x;
    stick_history.previous_y = y;
}

void ft0_record_position(uint8_t x, uint8_t y) {
    if(x == stick_history.previous_x && y == stick_history.previous_y)
        return;

    const int dx = (int)x - stick_history.previous_x;
    const int dy = (int)y - stick_history.previous_y;
    const float distance = sqrtf((float)(dx * dx + dy * dy));
    stick_point_t *point = &stick_history.points[stick_history.next];
    point->x = x;
    point->y = y;
    point->color = ft0_distance_color(distance);
    stick_history.next = (stick_history.next + 1u) % FT0_HISTORY_CAPACITY;

    // ranges persist until X is pressed, even after old dots leave the FIFO
    if(stick_history.count == 0) {
        stick_history.min_x = stick_history.max_x = x;
        stick_history.min_y = stick_history.max_y = y;
    }
    if(stick_history.count < FT0_HISTORY_CAPACITY)
        ++stick_history.count;
    if(x < stick_history.min_x) stick_history.min_x = x;
    if(x > stick_history.max_x) stick_history.max_x = x;
    if(y < stick_history.min_y) stick_history.min_y = y;
    if(y > stick_history.max_y) stick_history.max_y = y;
    stick_history.previous_x = x;
    stick_history.previous_y = y;
}
