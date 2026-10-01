#include <kos.h>
#include <dc/biosfont.h>
#include <dc/maple/controller.h>
#include <kos/irq.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ft8_tests.h"
#include "editor.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

typedef struct {
    maple_frame_t frame;
    uint32_t words[FT8_MAX_WORDS];
    int port;
    int unit;
    volatile bool complete;
    volatile int response;
    bool target_selected;
} rumble_transport_t;

static rumble_transport_t rumble_transport;
static ft8_test_t selected_test;

static uint32_t buttons(void) {
    maple_device_t *controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *state = controller ? maple_dev_status(controller) : NULL;

    return state ? state->buttons : 0;
}

static void text(unsigned row, const char *string, uint32_t color) {
    char clipped[49];

    snprintf(clipped, sizeof(clipped), "%s", string);
    bfont_draw_str_ex(vram_s + (24 + row * 24) * vid_mode->width + 24,
                      vid_mode->width, color, 0, 16, false, clipped);
}

static void reply(maple_state_t *state, maple_frame_t *frame) {
    const maple_response_t *response = (const maple_response_t *)frame->recv_buf;

    (void)state;
    rumble_transport.response = response->response;
    maple_frame_unlock(frame);
    rumble_transport.complete = true;
}

/* KOS resubmits FC without invoking the callback. Remove that request only
   while the DMA is idle; never reuse storage still owned by a live DMA. */
static bool retire_pending(maple_frame_t *frame) {
    bool retired = false;
    irq_mask_t mask = irq_disable();

    if(!maple_dma_in_progress() && frame->queued &&
       (frame->state == MAPLE_FRAME_UNSENT || frame->state == MAPLE_FRAME_SENT)) {
        maple_queue_remove(frame);
        frame->state = MAPLE_FRAME_VACANT;
        retired = true;
    }
    irq_restore(mask);
    return retired;
}

static int send_packet(void *context, const ft8_packet_t *packet) {
    rumble_transport_t *transport = context;
    maple_frame_t *frame = &transport->frame;
    uint64_t deadline = timer_ms_gettime64() + 750;

    if(!transport->target_selected || packet->count > FT8_MAX_WORDS)
        return -1;
    /* A previous timeout can still have an in-flight DMA. Wait for safe
       retirement before touching its frame or the fixed transmit words. */
    while(frame->state != MAPLE_FRAME_VACANT) {
        if(retire_pending(frame)) break;
        if(timer_ms_gettime64() >= deadline) return -1;
        thd_sleep(1);
    }
    if(maple_frame_trylock(frame) < 0) return -1;
    maple_frame_init(frame);
    for(size_t i = 0; i < packet->count; ++i)
        transport->words[i] = packet->words[i];
    transport->complete = false;
    frame->cmd = packet->command;
    frame->dst_port = transport->port;
    frame->dst_unit = transport->unit;
    frame->length = (int)packet->count;
    frame->send_buf = transport->words;
    frame->callback = reply;
    if(maple_queue_frame(frame) < 0) {
        frame->state = MAPLE_FRAME_VACANT; /* Not submitted to DMA. */
        return -1;
    }
    deadline = timer_ms_gettime64() + 750;
    while(!transport->complete) {
        if(frame->state == MAPLE_FRAME_UNSENT && frame->queued &&
           ((const maple_response_t *)frame->recv_buf)->response == MAPLE_RESPONSE_AGAIN &&
           retire_pending(frame))
            return 0xfc;
        if(timer_ms_gettime64() >= deadline) {
            retire_pending(frame);
            return -1;
        }
        thd_sleep(1);
    }
    return transport->response == MAPLE_RESPONSE_NONE ? -1 :
           (uint8_t)transport->response;
}

static uint32_t now_ms(void *context) {
    (void)context;
    return (uint32_t)timer_ms_gettime64();
}

static void wait_ms(void *context, unsigned milliseconds) {
    (void)context;
    thd_sleep(milliseconds);
}

static bool cancelled(void *context) {
    (void)context;
    return (buttons() & CONT_B) != 0;
}

static void stage(void *context, const char *label) {
    char line[64];

    (void)context;
    vid_clear(0, 0, 0);
    text(0, "FT8 Tremor Pack baseline", 0xffff);
    snprintf(line, sizeof(line), "Test %02u / %02u", selected_test + 1, FT8_TEST_COUNT);
    text(2, line, 0x07ff);
    text(4, label, 0xffe0);
    text(6, "Running... B cancels and stops vibration.", 0xbdf7);
    text(8, "Replies are recorded by your logic analyzer.", 0xbdf7);
}

static bool select_target(ft8_transport_t *transport) {
    maple_device_t *pack = maple_enum_type(0, MAPLE_FUNC_PURUPURU);
    unsigned function_index = 0;

    if(!pack) return false;
    rumble_transport.port = pack->port;
    rumble_transport.unit = pack->unit;
    rumble_transport.target_selected = true;
    /* Function-definition entries correspond to descending function bits.
       Source count is FD byte 0 in KOS host memory, not the high wire byte. */
    for(uint32_t function = 0x80000000u; function > FT8_FUNCTION; function >>= 1)
        if(pack->info.functions & function) ++function_index;
    transport->sources = function_index < 3 ?
                         pack->info.function_data[function_index] & 0xffu : 1;
    if(transport->sources < 1 || transport->sources > 15)
        transport->sources = 1;
    return true;
}

static void menu(ft8_test_t selected, const char *status) {
    const unsigned rows = 11;
    unsigned first = (unsigned)selected / rows * rows;
    char line[80];

    vid_clear(0, 0, 0);
    text(0, "FT8 Tremor Pack baseline", 0xffff);
    text(1, "Up/Down test  Left/Right page", 0xbdf7);
    text(2, "A run  X run ALL  Y editor  Start exit", 0xbdf7);
    snprintf(line, sizeof(line), "Page %u / %u (Kill runs last)", first / rows + 1,
             (FT8_TEST_COUNT + rows - 1) / rows);
    text(3, line, 0x07ff);
    for(unsigned i = first; i < FT8_TEST_COUNT && i < first + rows; ++i) {
        snprintf(line, sizeof(line), "%c %02u %s",
                 i == (unsigned)selected ? '>' : ' ', i + 1, ft8_test_names[i]);
        text(i - first + 5, line, i == (unsigned)selected ? 0xffe0 : 0xffff);
    }
    text(17, status, 0xbdf7);
}

static void editor_screen(const ft8_editor_t *editor, const char *status) {
    char line[80];
    char bits[33];
    char marker[33];
    unsigned first = editor->field > 5 ? editor->field - 5 : 0;
    uint8_t value = ft8_editor_value(editor, editor->field);

    vid_clear(0, 0, 0);
    text(0, ft8_editor_names[editor->mode], 0xffff);
    text(1, "Up/Down field  Left/Right bit  A toggle", 0xbdf7);
    text(2, "Hold Y + Left/Right: byte -/+1", 0xbdf7);
    text(3, "X send  Y read  B stop  Start back", 0xbdf7);
    for(unsigned i = 0; i < 8; ++i) {
        bits[i * 4] = value & (0x80u >> i) ? '1' : '0';
        marker[i * 4] = i == editor->bit ? '^' : ' ';
        for(unsigned j = 1; j < 4; ++j) {
            bits[i * 4 + j] = ' ';
            marker[i * 4 + j] = ' ';
        }
    }
    bits[32] = marker[32] = '\0';
    text(5, ft8_editor_bit_labels(editor), 0x07ff);
    text(6, bits, 0xffff);
    text(7, marker, 0xffe0);
    for(unsigned i = first; i < ft8_editor_field_count(editor) && i < first + 6; ++i) {
        snprintf(line, sizeof(line), "%c %02u %-29s %02X",
                 i == editor->field ? '>' : ' ', i,
                 ft8_editor_field_name(editor, i), ft8_editor_value(editor, i));
        text(9 + i - first, line, i == editor->field ? 0xffe0 : 0xffff);
    }
    if(editor->mode == FT8_EDIT_EFFECT) {
        unsigned power = (editor->packet.words[1] >> 8) & 0xff;
        unsigned frequency = (editor->packet.words[1] >> 16) & 0xff;
        snprintf(line, sizeof(line), "F=%u.%uHz P=%u M=%u Inc=%u cycles", (frequency + 1) / 2,
                 (frequency + 1) % 2 ? 5 : 0, (power >> 4) & 7, power & 7,
                 (unsigned)(editor->packet.words[1] >> 24));
        text(15, line, 0x07ff);
    }
    else if(editor->mode == FT8_EDIT_AST) {
        unsigned ast = ft8_editor_value(editor, 13);
        snprintf(line, sizeof(line), "First AST = %u ms (source per ASR mask)", (ast + 1) * 250);
        text(15, line, 0x07ff);
    }
    snprintf(line, sizeof(line), "%s  Repeat: %s (%u ms)", status,
             editor->repeating ? "ACTIVE" : "off", editor->active_repeat_ms);
    text(17, line, editor->repeating ? 0xffe0 : 0xbdf7);
}

static const char *send_status(int response) {
    if(response == -1) return "No response.";
    if(response >= 0xfc) return "Error reply captured.";
    return "Sent.";
}

static void edit_commands(ft8_transport_t *transport, ft8_editor_mode_t mode) {
    ft8_editor_t editor;
    uint32_t previous = buttons();
    bool modified = false;
    const char *status = "Ready.";

    ft8_editor_init(&editor, mode);
    editor_screen(&editor, status);
    while(true) {
        uint32_t current;
        uint32_t pressed;

        vid_waitvbl();
        current = buttons();
        pressed = current & ~previous;
        if(pressed & CONT_START) break;
        if(pressed & CONT_B) {
            ft8_packet_t stop = ft8_effect_packet((ft8_effect_t){.ctrl=0x10, .frequency=0x13});
            editor.repeating = false;
            status = send_status(transport->send(transport->context, &stop));
        }
        else {
            if(pressed & CONT_DPAD_UP)
                editor.field = editor.field ? editor.field - 1 : ft8_editor_field_count(&editor) - 1;
            if(pressed & CONT_DPAD_DOWN)
                editor.field = (editor.field + 1) % ft8_editor_field_count(&editor);
            if(current & CONT_Y) {
                if(pressed & CONT_Y) modified = false;
                if(pressed & CONT_DPAD_LEFT) { ft8_editor_change(&editor, false, -1); modified = true; }
                if(pressed & CONT_DPAD_RIGHT) { ft8_editor_change(&editor, false, 1); modified = true; }
            }
            else {
                if(pressed & CONT_DPAD_LEFT) editor.bit = (editor.bit + 7) % 8;
                if(pressed & CONT_DPAD_RIGHT) editor.bit = (editor.bit + 1) % 8;
            }
            if(pressed & CONT_A) ft8_editor_change(&editor, true, 0);
            /* Read on release, so holding Y for numeric edits never sends a read. */
            if((previous & CONT_Y) && !(current & CONT_Y) && !modified) {
                ft8_packet_t packet = ft8_editor_read_packet(&editor);
                status = send_status(transport->send(transport->context, &packet));
            }
            if(pressed & CONT_X) {
                status = send_status(transport->send(transport->context, &editor.packet));
                ft8_editor_begin_repeat(&editor, transport->now_ms(transport->context));
            }
            if(ft8_editor_repeat_due(&editor, transport->now_ms(transport->context)))
                status = send_status(transport->send(transport->context, &editor.repeated_packet));
        }
        if(current != previous || pressed || editor.repeating)
            editor_screen(&editor, status);
        previous = current;
    }
    editor.repeating = false;
    ft8_stop_restore(transport);
}

static void editor_picker(ft8_transport_t *transport) {
    ft8_editor_mode_t mode = FT8_EDIT_EFFECT;
    uint32_t previous = buttons();
    bool redraw = true;

    while(true) {
        uint32_t current;
        uint32_t pressed;
        if(redraw) {
            vid_clear(0, 0, 0);
            text(0, "FT8 command editor", 0xffff);
            text(2, "Up/Down profile  A edit  B back", 0xbdf7);
            for(unsigned i = 0; i < FT8_EDIT_COUNT; ++i) {
                char line[64];
                snprintf(line, sizeof(line), "%c %s", i == (unsigned)mode ? '>' : ' ', ft8_editor_names[i]);
                text(5 + i * 2, line, i == (unsigned)mode ? 0xffe0 : 0xffff);
            }
            text(16, "All fields remain sendable, even invalid bits.", 0xbdf7);
            redraw = false;
        }
        vid_waitvbl();
        current = buttons();
        pressed = current & ~previous;
        previous = current;
        if(pressed & (CONT_B | CONT_START)) break;
        if(pressed & CONT_DPAD_UP) mode = mode ? mode - 1 : FT8_EDIT_COUNT - 1;
        if(pressed & CONT_DPAD_DOWN) mode = (mode + 1) % FT8_EDIT_COUNT;
        if((pressed & CONT_A) && select_target(transport)) {
            edit_commands(transport, mode);
            previous = buttons();
        }
        if(pressed) redraw = true;
    }
}

int main(void) {
    ft8_transport_t transport = {
        .send=send_packet, .now_ms=now_ms, .wait_ms=wait_ms,
        .cancelled=cancelled, .stage=stage,
        .context=&rumble_transport, .sources=1
    };
    uint32_t previous = 0;
    char status[80] = "Ready. Connect controller + Tremor Pack.";

    vid_set_mode(DM_640x480, PM_RGB565);
    menu(selected_test, status);
    while(true) {
        uint32_t current;
        uint32_t pressed;

        vid_waitvbl();
        current = buttons();
        pressed = current & ~previous;
        previous = current;
        if(pressed & CONT_START) break;
        if(pressed & CONT_DPAD_UP)
            selected_test = selected_test ? selected_test - 1 : FT8_TEST_COUNT - 1;
        if(pressed & CONT_DPAD_DOWN)
            selected_test = (selected_test + 1) % FT8_TEST_COUNT;
        if(pressed & (CONT_DPAD_RIGHT | CONT_DPAD_LEFT))
            selected_test = selected_test < 11 ? 11 : 0;
        if(pressed & CONT_Y) {
            editor_picker(&transport);
            previous = buttons();
        }
        if(pressed & (CONT_A | CONT_X)) {
            if(!select_target(&transport))
                snprintf(status, sizeof(status), "Connect a rumble pack, then press A.");
            else {
                bool all = (pressed & CONT_X) != 0;
                ft8_result_t total = {0};
                if(all) selected_test = FT8_TEST_IDENTITY;
                do {
                    ft8_result_t result = ft8_test_run(selected_test, &transport);
                    total.requests += result.requests;
                    total.errors += result.errors;
                    total.missing += result.missing;
                    total.cancelled |= result.cancelled;
                    if(!all || total.cancelled || selected_test == FT8_TEST_KILL) break;
                    ++selected_test;
                    thd_sleep(250);
                } while(true);
                if(selected_test == FT8_TEST_KILL && !total.cancelled)
                    snprintf(status, sizeof(status), "Kill sent. Unplug/replug pack to continue.");
                else
                    snprintf(status, sizeof(status), "%s %u requests, %u errors, %u no reply.",
                             total.cancelled ? "Cancelled." : "Done.",
                             total.requests, total.errors, total.missing);
                previous = buttons();
            }
        }
        if(pressed) menu(selected_test, status);
    }
    if(rumble_transport.target_selected && selected_test != FT8_TEST_KILL)
        ft8_stop_restore(&transport);
    return 0;
}
