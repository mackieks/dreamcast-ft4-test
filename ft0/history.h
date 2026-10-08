#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FT0_HISTORY_CAPACITY 512u
#define FT0_RED_DISTANCE 32.0f

typedef struct {
    uint8_t x;
    uint8_t y;
    uint16_t color;
} stick_point_t;

typedef struct {
    stick_point_t points[FT0_HISTORY_CAPACITY];
    unsigned next;
    unsigned count;
    uint8_t previous_x;
    uint8_t previous_y;
    uint8_t min_x;
    uint8_t max_x;
    uint8_t min_y;
    uint8_t max_y;
} stick_history_t;

extern stick_history_t stick_history;

void ft0_clear_history(uint8_t x, uint8_t y);
void ft0_record_position(uint8_t x, uint8_t y);
uint16_t ft0_distance_color(float distance);
