#include "editor.h"

const char *const ft8_editor_names[FT8_EDIT_COUNT] = {
    "Set_Condition (CTRL/POW/Freq/Inc)", "AST (ASR/source/time)",
    "Waveform (20 x 10ms steps)", "Get_Media_Info (source)",
    "Raw Maple command / payload"
};

void ft8_editor_init(ft8_editor_t *editor, ft8_editor_mode_t mode) {
    *editor = (ft8_editor_t){.mode = mode, .field = 7};
    switch(mode) {
        case FT8_EDIT_EFFECT:
            editor->packet = ft8_effect_packet((ft8_effect_t){
                .ctrl=0x11, .power=0x70, .frequency=0x13, .increment=0});
            break;
        case FT8_EDIT_AST:
            editor->packet = ft8_ast_packet(0x0002,
                (const uint8_t[]){FT8_DEFAULT_AST}, 1);
            editor->field = 13; /* AST: payload byte 10. */
            break;
        case FT8_EDIT_WAVEFORM:
            editor->packet.command = FT8_WRITE;
            editor->packet.count = 7;
            editor->packet.words[0] = FT8_FUNCTION;
            editor->packet.words[1] = 1;
            for(unsigned i = 0; i < 20; ++i) {
                uint8_t step = i == 19 ? 0x40 : 7;
                editor->packet.words[2 + i / 4] |= (uint32_t)step << (8 * (i % 4));
            }
            editor->field = 11;
            break;
        case FT8_EDIT_MEDIA:
            editor->packet = ft8_query_packet(FT8_MEDIA, 1);
            break;
        case FT8_EDIT_RAW:
            editor->packet = ft8_query_packet(FT8_GETCOND, 0);
            editor->field = 0;
            break;
        case FT8_EDIT_COUNT:
            break;
    }
}

unsigned ft8_editor_field_count(const ft8_editor_t *editor) {
    return 3 + (unsigned)editor->packet.count * 4;
}

uint8_t ft8_editor_value(const ft8_editor_t *editor, unsigned field) {
    if(field == 0) return editor->packet.command;
    if(field == 1) return (uint8_t)editor->packet.count;
    if(field == 2) return editor->repeat_units;
    field -= 3;
    if(field >= FT8_MAX_WORDS * 4) return 0;
    return (uint8_t)(editor->packet.words[field / 4] >> (8 * (field % 4)));
}

void ft8_editor_change(ft8_editor_t *editor, bool toggle, int delta) {
    uint8_t value = ft8_editor_value(editor, editor->field);
    unsigned byte;
    unsigned shift;

    if(toggle)
        value ^= (uint8_t)(0x80u >> editor->bit);
    else
        value = (uint8_t)(value + delta);
    if(editor->field == 0) editor->packet.command = value;
    else if(editor->field == 1) {
        /* Physical storage limit only; do not sanitize protocol fields. */
        editor->packet.count = value > FT8_MAX_WORDS ? FT8_MAX_WORDS : value;
    }
    else if(editor->field == 2) editor->repeat_units = value;
    else {
        byte = editor->field - 3;
        if(byte >= FT8_MAX_WORDS * 4) return;
        shift = 8 * (byte % 4);
        editor->packet.words[byte / 4] &= ~(0xffu << shift);
        editor->packet.words[byte / 4] |= (uint32_t)value << shift;
    }
}

const char *ft8_editor_field_name(const ft8_editor_t *editor, unsigned field) {
    if(field == 0) return "Command";
    if(field == 1) return "Length (words, 0-8)";
    if(field == 2) return "Repeat (x50ms; 0=off)";
    if(field < 7) return "Function byte";
    if(editor->mode == FT8_EDIT_EFFECT) {
        switch(field) {
            case 7: return "CTRL";
            case 8: return "POW";
            case 9: return "Freq";
            case 10: return "Inc";
        }
    }
    else if(editor->mode == FT8_EDIT_AST || editor->mode == FT8_EDIT_WAVEFORM) {
        switch(field) {
            case 7: return "VN (0=AST, 1=waveform)";
            case 8: return "Phase";
            case 9: return "Block high";
            case 10: return "Block low";
        }
        if(editor->mode == FT8_EDIT_AST) {
            if(field == 11) return "ASR high";
            if(field == 12) return "ASR low";
            if(field == 13) return "AST / first selected source";
            if(field == 14) return "Padding / next AST";
        }
        else if(field >= 11) return "Waveform step";
    }
    else if(editor->mode == FT8_EDIT_MEDIA && field == 7) return "VN";
    return "Payload byte";
}

const char *ft8_editor_bit_labels(const ft8_editor_t *editor) {
    if(editor->mode == FT8_EDIT_EFFECT && editor->field == 7)
        return "VN3 VN2 VN1 VN0 R   R   R   CNT";
    if(editor->mode == FT8_EDIT_EFFECT && editor->field == 8)
        return "INH P2  P1  P0  EXH M2  M1  M0";
    if(editor->mode == FT8_EDIT_WAVEFORM && editor->field >= 11)
        return "CNT WFE R   R   DIR P2  P1  P0";
    return "b7  b6  b5  b4  b3  b2  b1  b0";
}

ft8_packet_t ft8_editor_read_packet(const ft8_editor_t *editor) {
    if(editor->mode == FT8_EDIT_AST || editor->mode == FT8_EDIT_WAVEFORM)
        return ft8_query_packet(FT8_READ, editor->packet.words[1]);
    if(editor->mode == FT8_EDIT_MEDIA)
        return editor->packet;
    return ft8_query_packet(FT8_GETCOND, 0);
}

void ft8_editor_begin_repeat(ft8_editor_t *editor, uint32_t now) {
    editor->repeating = editor->repeat_units != 0;
    editor->active_repeat_ms = editor->repeat_units * 50u;
    editor->repeat_deadline = now + editor->active_repeat_ms;
    editor->repeated_packet = editor->packet;
}

bool ft8_editor_repeat_due(ft8_editor_t *editor, uint32_t now) {
    if(!editor->repeating || (int32_t)(now - editor->repeat_deadline) < 0)
        return false;
    /* Never burst overdue sends. Editing does not mutate an active resend. */
    editor->repeat_deadline = now + editor->active_repeat_ms;
    return true;
}
