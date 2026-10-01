#ifndef FT8_EDITOR_H
#define FT8_EDITOR_H

#include "ft8_tests.h"

typedef enum {
    FT8_EDIT_EFFECT, FT8_EDIT_AST, FT8_EDIT_WAVEFORM,
    FT8_EDIT_MEDIA, FT8_EDIT_RAW, FT8_EDIT_COUNT
} ft8_editor_mode_t;

typedef struct {
    ft8_editor_mode_t mode;
    ft8_packet_t packet;
    unsigned field;
    unsigned bit; /* 0 selects bit 7, 7 selects bit 0. */
    uint8_t repeat_units; /* 50ms units; zero means one send. */
    bool repeating;
    uint32_t repeat_deadline;
    unsigned active_repeat_ms;
    ft8_packet_t repeated_packet;
} ft8_editor_t;

extern const char *const ft8_editor_names[FT8_EDIT_COUNT];
void ft8_editor_init(ft8_editor_t *editor, ft8_editor_mode_t mode);
unsigned ft8_editor_field_count(const ft8_editor_t *editor);
uint8_t ft8_editor_value(const ft8_editor_t *editor, unsigned field);
void ft8_editor_change(ft8_editor_t *editor, bool toggle, int delta);
const char *ft8_editor_field_name(const ft8_editor_t *editor, unsigned field);
const char *ft8_editor_bit_labels(const ft8_editor_t *editor);
ft8_packet_t ft8_editor_read_packet(const ft8_editor_t *editor);
void ft8_editor_begin_repeat(ft8_editor_t *editor, uint32_t now);
bool ft8_editor_repeat_due(ft8_editor_t *editor, uint32_t now);

#endif
