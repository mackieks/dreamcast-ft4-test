#include <kos.h>
#include <dc/biosfont.h>
#include <dc/maple/controller.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ft4_tests.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

typedef struct {
    maple_frame_t frame;
    uint32_t words[2];
    int port;
    int unit;
    volatile bool complete;
    volatile int response;
} mic_transport_t;

static mic_transport_t mic_transport;

static uint32_t buttons(void) {
    maple_device_t *controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *state = controller ? maple_dev_status(controller) : NULL;

    return state ? state->buttons : 0;
}

static void reply(maple_state_t *state, maple_frame_t *frame) {
    mic_transport_t *transport = &mic_transport;
    const maple_response_t *response = (const maple_response_t *)frame->recv_buf;

    (void)state;
    transport->response = response->response;
    maple_frame_unlock(frame);
    transport->complete = true;
}

static bool send_packet(void *context, uint8_t command,
                        const uint32_t *words, size_t count) {
    mic_transport_t *transport = context;
    maple_frame_t *frame = &transport->frame;
    uint64_t deadline = timer_ms_gettime64() + 750;

    if(count > 2 || frame->state != MAPLE_FRAME_VACANT)
        return false;

    /* This frame belongs to the application. Raw Basic_Control never changes
       KOS's SIP is_sampling flag, so its periodic sample poll stays inactive. */
    if(maple_frame_lock(frame) < 0)
        return false;
    maple_frame_init(frame);

    for(size_t i = 0; i < count; ++i)
        transport->words[i] = words[i];

    transport->complete = false;
    frame->cmd = command;
    frame->dst_port = transport->port;
    frame->dst_unit = transport->unit;
    frame->length = (int)count;
    frame->send_buf = transport->words;
    frame->callback = reply;
    maple_queue_frame(frame);

    while(!transport->complete) {
        if(timer_ms_gettime64() >= deadline)
            return false;
        thd_sleep(1);
    }

    /* Preserve error replies as wire evidence and continue the sequence.
       Only no-response/transport failure aborts it. */
    return transport->response != MAPLE_RESPONSE_NONE;
}

static void wait_frame(void *context) {
    (void)context;
    vid_waitvbl();
}

static bool cancelled(void *context) {
    (void)context;
    return (buttons() & CONT_B) != 0;
}

static void text(unsigned row, const char *string, uint32_t color) {
    bfont_draw_str_ex(vram_s + (24 + row * 24) * vid_mode->width + 24,
                      vid_mode->width, color, 0, 16, false, string);
}

static void menu(ft4_test_t selected, bool low_rate, const char *status) {
    char line[64];

    vid_clear(0, 0, 0);
    text(0, "FT4 microphone packet tests", 0xffff);
    text(1, "Up/Down test  Left/Right rate", 0xbdf7);
    text(2, "A run  B cancel  Start exit", 0xbdf7);
    text(3, low_rate ? "Rate: 8 kHz" : "Rate: 11 kHz", 0x07ff);

    for(unsigned i = 0; i < FT4_TEST_COUNT; ++i) {
        snprintf(line, sizeof(line), "%c %s", i == (unsigned)selected ? '>' : ' ',
                 ft4_test_names[i]);
        text(i + 5, line, i == (unsigned)selected ? 0xffe0 : 0xffff);
    }
    text(17, status, 0xbdf7);
}

int main(void) {
    const ft4_transport_t transport = {
        .send = send_packet,
        .wait_frame = wait_frame,
        .cancelled = cancelled,
        .context = &mic_transport
    };
    ft4_test_t selected = FT4_TEST_IDENTITY;
    bool low_rate = true;
    uint32_t previous = 0;
    const char *status = "Ready. Connect controller + mic.";

    vid_set_mode(DM_640x480, PM_RGB565);
    menu(selected, low_rate, status);

    while(true) {
        uint32_t current;
        uint32_t pressed;

        vid_waitvbl();
        current = buttons();
        pressed = current & ~previous;
        previous = current;

        if(pressed & CONT_START)
            break;
        if(pressed & CONT_DPAD_UP)
            selected = selected == 0 ? FT4_TEST_COUNT - 1 : selected - 1;
        if(pressed & CONT_DPAD_DOWN)
            selected = (selected + 1) % FT4_TEST_COUNT;
        if(pressed & (CONT_DPAD_LEFT | CONT_DPAD_RIGHT))
            low_rate = !low_rate;

        if(pressed & CONT_A) {
            maple_device_t *mic = maple_enum_type(0, MAPLE_FUNC_MICROPHONE);

            if(!mic)
                status = "Connect a microphone, then press A.";
            else {
                mic_transport.port = mic->port;
                mic_transport.unit = mic->unit;
                menu(selected, low_rate, "Running... B cancels.");
                status = ft4_test_run(selected, low_rate, &transport) ?
                         "Done." : "Cancelled or transfer failed.";
                previous = buttons();
            }
        }

        if(pressed)
            menu(selected, low_rate, status);
    }
    return 0;
}
