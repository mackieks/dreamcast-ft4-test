#include "ft8_tests.h"
#include "editor.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    ft8_packet_t packets[1024];
    uint32_t times[1024];
    size_t count;
    uint32_t now;
    uint32_t cancel_at;
    bool inject_errors;
    bool inject_missing;
} capture_t;

static capture_t capture;

static int send_packet(void *context, const ft8_packet_t *packet) {
    capture_t *trace = context;

    assert(packet->count <= FT8_MAX_WORDS);
    assert(trace->count < 1024);
    trace->times[trace->count] = trace->now;
    trace->packets[trace->count++] = *packet;
    if(trace->inject_missing) return -1;
    if(trace->inject_errors) return 0xfd;
    return 7;
}

static uint32_t now_ms(void *context) { return ((capture_t *)context)->now; }
static void wait_ms(void *context, unsigned milliseconds) {
    ((capture_t *)context)->now += milliseconds;
}
static bool cancelled(void *context) {
    capture_t *trace = context;
    return trace->cancel_at && trace->now >= trace->cancel_at;
}
static void stage(void *context, const char *label) {
    (void)context;
    assert(label && label[0]);
}

static ft8_transport_t transport = {
    .send=send_packet, .now_ms=now_ms, .wait_ms=wait_ms,
    .cancelled=cancelled, .stage=stage, .context=&capture, .sources=1
};

static void assert_cleanup(void) {
    const ft8_packet_t *stop = &capture.packets[capture.count - 2];
    const ft8_packet_t *ast = &capture.packets[capture.count - 1];

    assert(stop->command == FT8_SETCOND && stop->count == 2);
    assert(stop->words[0] == FT8_FUNCTION && stop->words[1] == 0x00130010u);
    assert(ast->command == FT8_WRITE && ast->count == 3);
    assert(ast->words[0] == FT8_FUNCTION && ast->words[1] == 0);
    assert(ast->words[2] == 0x00130200u);
}

static void check_packing(void) {
    ft8_packet_t packet = ft8_effect_packet((ft8_effect_t){
        .ctrl=0x11, .power=0x70, .frequency=0x27, .increment=0});
    uint8_t times[15];
    const uint8_t wire[] = {0x00,0x01,0x00,0x00, 0x00,0x27,0x70,0x11};

    assert(packet.command == FT8_SETCOND && packet.count == 2);
    assert(packet.words[0] == 0x00010000u && packet.words[1] == 0x00277011u);
    for(unsigned i = 0; i < sizeof(wire); ++i)
        assert(((packet.words[i / 4] >> (24 - 8 * (i % 4))) & 0xff) == wire[i]);
    packet = ft8_ast_packet(0x0002, (const uint8_t[]){0x13}, 1);
    assert(packet.count == 3 && packet.words[2] == 0x00130200u);
    packet = ft8_ast_packet(0x8002, (const uint8_t[]){0x01,0xff}, 2);
    assert(packet.count == 3 && packet.words[2] == 0xff010280u);
    packet = ft8_ast_packet(0, NULL, 0);
    assert(packet.count == 3 && packet.words[2] == 0);
    for(unsigned i = 0; i < 15; ++i) times[i] = (uint8_t)(i + 1);
    packet = ft8_ast_packet(0xfffe, times, 15);
    assert(packet.count == 7);
    assert(packet.words[2] == 0x0201feffu);
    assert(packet.words[6] == 0x0000000fu); /* Final AST, three zero padding bytes. */
    assert(ft8_ast_packet(0xffff, times, 16).count == 0);
    packet = ft8_query_packet(FT8_MEDIA, 1);
    assert(packet.count == 2 && packet.words[1] == 1);
    assert(ft8_query_packet(FT8_GETCOND, 0).count == 1);
    assert(ft8_query_packet(FT8_RESET, 0).count == 0);
}

static void check_sequences(void) {
    bool commands[256] = {false};
    for(ft8_test_t test = 0; test < FT8_TEST_COUNT; ++test) {
        ft8_result_t result;
        memset(&capture, 0, sizeof(capture));
        result = ft8_test_run(test, &transport);
        assert(result.requests == capture.count);
        assert(!result.cancelled && !result.errors && !result.missing);
        assert(capture.packets[0].command == (test == FT8_TEST_IDENTITY ? FT8_DEVINFO : FT8_RESET));
        if(test != FT8_TEST_IDENTITY) {
            assert(capture.packets[1].command == FT8_DEVINFO);
            assert(capture.times[1] == 100);
        }
        for(size_t i = 0; i < capture.count; ++i)
            if(capture.packets[i].command == FT8_RESET) {
                /* No function request may precede re-enumeration after reset. */
                assert(i + 1 < capture.count);
                assert(capture.packets[i + 1].command == FT8_DEVINFO);
            }
        for(size_t i = 0; i < capture.count; ++i)
            commands[capture.packets[i].command] = true;
        if(test == FT8_TEST_KILL) {
            assert(capture.packets[capture.count - 2].command == FT8_KILL);
            assert(capture.packets[capture.count - 1].command == FT8_DEVINFO);
        }
        else assert_cleanup();
    }
    assert(commands[1] && commands[2] && commands[3] && commands[4]);
    assert(commands[9] && commands[10] && commands[11] && commands[12]);
    assert(commands[13] && commands[14] && commands[0xfc]);

    memset(&capture, 0, sizeof(capture));
    transport.sources = 3;
    ft8_test_run(FT8_TEST_IDENTITY, &transport);
    assert(capture.packets[2].words[1] == 1);
    assert(capture.packets[3].words[1] == 2);
    assert(capture.packets[4].words[1] == 3);
    transport.sources = 1;

    memset(&capture, 0, sizeof(capture));
    ft8_test_run(FT8_TEST_RESET_START, &transport);
    {
        unsigned resets = 0;
        for(size_t i = 0; i < capture.count; ++i)
            if(capture.packets[i].command == FT8_RESET) {
                assert(capture.packets[i + 1].command == FT8_DEVINFO);
                assert(capture.packets[i + 2].command == FT8_GETCOND);
                assert(capture.packets[i + 3].command == FT8_READ);
                ++resets;
            }
        assert(resets == 2);
        assert(capture.packets[4].command == FT8_SETCOND);
    }

    memset(&capture, 0, sizeof(capture));
    ft8_test_run(FT8_TEST_RAMPS_NEGATIVE, &transport);
    {
        unsigned seen = 0;
        const uint32_t expected[] = {0x01138710,0x01130910,0x02138710,
                                     0x02130910,0x04138710,0x04130910};
        for(size_t i = 0; i < capture.count - 2; ++i)
            if(capture.packets[i].command == FT8_SETCOND) {
                assert(seen < 6 && capture.packets[i].words[1] == expected[seen]);
                ++seen;
            }
        assert(seen == 6);
    }
    memset(&capture, 0, sizeof(capture));
    ft8_test_run(FT8_TEST_INVALID_EFFECTS, &transport);
    {
        bool both = false, inc_zero = false, inc_above_freq = false;
        for(size_t i = 0; i < capture.count; ++i) {
            uint32_t word = capture.packets[i].words[1];
            if(capture.packets[i].command != FT8_SETCOND) continue;
            both |= word == 0x0113f811;
            inc_zero |= word == 0x0013f011;
            inc_above_freq |= word == 0x0a07f011;
        }
        assert(both && inc_zero && inc_above_freq);
    }
    memset(&capture, 0, sizeof(capture));
    ft8_test_run(FT8_TEST_WAVEFORM, &transport);
    {
        unsigned seen = 0;
        for(size_t i = 0; i < capture.count; ++i) {
            ft8_packet_t packet = capture.packets[i];
            if(packet.command != FT8_WRITE || packet.words[1] != 1) continue;
            assert(packet.count == 7);
            assert(packet.words[2] == (seen ? 0x07070787u : 0x07070707u));
            assert(packet.words[6] == 0x400b0b0bu);
            ++seen;
        }
        assert(seen == 2);
    }
}

static void check_ast_timing(void) {
    memset(&capture, 0, sizeof(capture));
    ft8_test_run(FT8_TEST_AST_ACTIVE_CHANGE, &transport);
    {
        uint32_t starts[2] = {0};
        uint32_t change = 0;
        unsigned found = 0;
        for(size_t i = 0; i < capture.count - 2; ++i) {
            if(capture.packets[i].command == FT8_SETCOND) starts[found++] = capture.times[i];
            if(capture.packets[i].command == FT8_WRITE && capture.packets[i].words[2] == 0x00000200)
                change = capture.times[i];
        }
        assert(found == 2 && change - starts[0] == 200);
        assert(starts[1] - starts[0] == 1300);
    }
    memset(&capture, 0, sizeof(capture));
    ft8_test_run(FT8_TEST_AST_REFRESH, &transport);
    {
        unsigned found = 0;
        uint32_t previous = 0;
        for(size_t i = 0; i < capture.count - 2; ++i) {
            if(capture.packets[i].command != FT8_SETCOND) continue;
            if(found) assert(capture.times[i] - previous == 300);
            previous = capture.times[i];
            ++found;
        }
        assert(found == 6);
    }
    memset(&capture, 0, sizeof(capture));
    ft8_test_run(FT8_TEST_AST_POLL, &transport);
    {
        unsigned settings = 0, polls = 0;
        for(size_t i = 0; i < capture.count - 2; ++i) {
            settings += capture.packets[i].command == FT8_SETCOND;
            polls += capture.packets[i].command == FT8_GETCOND;
        }
        assert(settings == 1 && polls >= 18);
    }
}

static void check_cleanup_and_errors(void) {
    for(ft8_test_t test = 1; test < FT8_TEST_COUNT; ++test) {
        ft8_result_t result;
        memset(&capture, 0, sizeof(capture));
        capture.cancel_at = 1;
        result = ft8_test_run(test, &transport);
        assert(result.cancelled);
        assert_cleanup();
    }
    memset(&capture, 0, sizeof(capture));
    capture.cancel_at = 400;
    assert(ft8_test_run(FT8_TEST_RAMPS_POSITIVE, &transport).cancelled);
    assert_cleanup();
    memset(&capture, 0, sizeof(capture));
    capture.inject_errors = true;
    {
        ft8_result_t result = ft8_test_run(FT8_TEST_INVALID_EFFECTS, &transport);
        assert(result.errors == result.requests && result.requests > 30);
        assert_cleanup();
    }
    memset(&capture, 0, sizeof(capture));
    capture.inject_missing = true;
    {
        ft8_result_t result = ft8_test_run(FT8_TEST_INVALID_PACKETS, &transport);
        assert(result.missing == result.requests && result.requests > 15);
        assert_cleanup();
    }
    memset(&capture, 0, sizeof(capture));
    ft8_stop_restore(&transport);
    assert_cleanup();
}

static void check_editor(void) {
    ft8_editor_t editor;
    ft8_editor_init(&editor, FT8_EDIT_EFFECT);
    assert(editor.packet.words[1] == 0x00137011u);
    editor.field = 8; /* POW */
    editor.bit = 0; /* INH */
    ft8_editor_change(&editor, true, 0);
    assert(editor.packet.words[1] == 0x0013f011u);
    editor.bit = 4; /* EXH */
    ft8_editor_change(&editor, true, 0);
    assert(editor.packet.words[1] == 0x0013f811u); /* Invalid combination preserved. */
    assert(strcmp(ft8_editor_bit_labels(&editor), "INH P2  P1  P0  EXH M2  M1  M0") == 0);
    editor.field = 9;
    ft8_editor_change(&editor, false, -1);
    assert(ft8_editor_value(&editor, 9) == 0x12);
    editor.repeat_units = 2;
    ft8_editor_begin_repeat(&editor, 0xfffffff0u);
    assert(!ft8_editor_repeat_due(&editor, 0x53));
    assert(ft8_editor_repeat_due(&editor, 0x54));
    editor.packet.words[1] = 0;
    editor.repeat_units = 10;
    assert(editor.repeated_packet.words[1] == 0x0012f811u);
    assert(editor.active_repeat_ms == 100);
    assert(ft8_editor_repeat_due(&editor, 0x1000));
    assert(!ft8_editor_repeat_due(&editor, 0x1000)); /* No catch-up burst. */
    editor.repeating = false;
    assert(!ft8_editor_repeat_due(&editor, 0x2000));
    editor.field = 1;
    ft8_editor_change(&editor, false, 100);
    assert(editor.packet.count == 8 && ft8_editor_field_count(&editor) == 35);
    editor.field = 34;
    editor.bit = 7;
    ft8_editor_change(&editor, true, 0);
    assert(editor.packet.words[7] == 0x01000000u);
    ft8_editor_init(&editor, FT8_EDIT_AST);
    assert(editor.packet.words[2] == 0x00130200u);
    assert(ft8_editor_read_packet(&editor).command == FT8_READ);
    ft8_editor_init(&editor, FT8_EDIT_WAVEFORM);
    assert(editor.packet.count == 7 && editor.packet.words[6] == 0x40070707u);
    assert(ft8_editor_read_packet(&editor).words[1] == 1);
    ft8_editor_init(&editor, FT8_EDIT_RAW);
    assert(editor.packet.command == FT8_GETCOND);
    editor.bit = 0;
    ft8_editor_change(&editor, true, 0);
    assert(editor.packet.command == 0x89); /* Unknown commands are sendable. */
}

int main(void) {
    check_packing();
    check_sequences();
    check_ast_timing();
    check_cleanup_and_errors();
    check_editor();
    puts("FT8 packet, sequence, timing, cleanup and editor checks passed.");
    return 0;
}
