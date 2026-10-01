#include "ft8_tests.h"

const char *const ft8_test_names[FT8_TEST_COUNT] = {
    "Cold identity and defaults", "Reset and direct startup",
    "Positive levels 0-7", "Negative levels 0-7",
    "Frequency sweep", "One-shot / continuous / default AST",
    "Positive converge / diverge", "Negative converge / diverge",
    "Continuous ramp envelopes", "Replace and zero-power stop",
    "AST read/write boundaries", "AST 250ms / 1s / 5s expiry",
    "AST refreshed by Set_Condition", "AST unchanged by polling",
    "Change AST during an effect", "AST source masks",
    "Invalid effect fields", "Invalid function / length / address",
    "Arbitrary waveform probe", "Unknown command and retry",
    "Kill LAST - unplug/replug afterward"
};

uint32_t ft8_host_word(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    return (uint32_t)b0 | ((uint32_t)b1 << 8) |
           ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24);
}

ft8_packet_t ft8_effect_packet(ft8_effect_t effect) {
    ft8_packet_t packet = {.command = FT8_SETCOND, .count = 2};

    packet.words[0] = FT8_FUNCTION;
    /* KOS host bytes CTRL, POW, Freq, Inc; Maple reverses each word. */
    packet.words[1] = ft8_host_word(effect.ctrl, effect.power,
                                   effect.frequency, effect.increment);
    return packet;
}

ft8_packet_t ft8_ast_packet(uint16_t mask, const uint8_t *times, size_t count) {
    ft8_packet_t packet = {.command = FT8_WRITE};

    if(count > 15 || (count && !times))
        return packet;
    packet.count = 2 + (2 + count + 3) / 4;
    packet.words[0] = FT8_FUNCTION;
    packet.words[1] = 0; /* VN=0, Phase=0, Block=0: auto-stop settings. */
    /* ASR is big endian within the host byte stream. ASTs follow in
       ascending source order; even unsupported sources occupy a byte. */
    packet.words[2] = ft8_host_word((uint8_t)(mask >> 8), (uint8_t)mask, 0, 0);
    for(size_t i = 0; i < count; ++i) {
        size_t byte = i + 2;
        packet.words[2 + byte / 4] |= (uint32_t)times[i] << (8 * (byte % 4));
    }
    return packet;
}

ft8_packet_t ft8_query_packet(uint8_t command, uint32_t address) {
    ft8_packet_t packet = {.command = command};

    if(command == FT8_GETCOND) {
        packet.count = 1;
        packet.words[0] = FT8_FUNCTION;
    }
    else if(command == FT8_MEDIA || command == FT8_READ) {
        packet.count = 2;
        packet.words[0] = FT8_FUNCTION;
        packet.words[1] = address;
    }
    return packet;
}

void ft8_stop_restore(const ft8_transport_t *transport) {
    ft8_effect_t stop = {.ctrl = 0x10, .power = 0, .frequency = 0x13,
                         .increment = 0};
    uint8_t ast = FT8_DEFAULT_AST;
    ft8_packet_t packet = ft8_effect_packet(stop);

    transport->send(transport->context, &packet);
    packet = ft8_ast_packet(0x0002, &ast, 1);
    transport->send(transport->context, &packet);
}

static void send_observe(const ft8_transport_t *transport, ft8_result_t *result,
                         ft8_packet_t packet) {
    int response = transport->send(transport->context, &packet);

    ++result->requests;
    if(response == -1)
        ++result->missing;
    else if((uint8_t)response >= 0xfc)
        ++result->errors;
}

static bool wait_observe(const ft8_transport_t *transport, ft8_result_t *result,
                         unsigned milliseconds, bool poll) {
    uint32_t start = transport->now_ms(transport->context);
    uint32_t next_poll = 0;

    while((uint32_t)(transport->now_ms(transport->context) - start) < milliseconds) {
        uint32_t elapsed = transport->now_ms(transport->context) - start;

        if(transport->cancelled(transport->context)) {
            result->cancelled = true;
            return false;
        }
        if(poll && elapsed >= next_poll) {
            send_observe(transport, result, ft8_query_packet(FT8_GETCOND, 0));
            next_poll = elapsed + 100;
        }
        transport->wait_ms(transport->context, 10);
    }
    return true;
}

static void set_ast(const ft8_transport_t *transport, ft8_result_t *result,
                    uint8_t ast) {
    send_observe(transport, result, ft8_ast_packet(0x0002, &ast, 1));
    send_observe(transport, result, ft8_query_packet(FT8_READ, 0));
}

ft8_result_t ft8_test_run(ft8_test_t test, const ft8_transport_t *transport) {
    ft8_result_t result = {0};
    ft8_effect_t effect = {.ctrl = 0x11, .power = 0x70,
                           .frequency = 0x13, .increment = 0};
    ft8_packet_t packet;
    unsigned sources = transport->sources;

    if(test >= FT8_TEST_COUNT)
        return result;
    if(sources < 1 || sources > 15)
        sources = 1;
    transport->stage(transport->context, ft8_test_names[test]);

    /* Cold identity deliberately precedes reset and all state-changing writes. */
    if(test != FT8_TEST_IDENTITY) {
        send_observe(transport, &result, ft8_query_packet(FT8_RESET, 0));
        if(!wait_observe(transport, &result, 100, false))
            goto cleanup;
        /* FT8 section 6.1.8 / MAPLE82E section 3.5: soft reset puts the
           peripheral back in standby until Device_Request completes its AP.
           Tremor test 2 confirmed silence without this request. */
        send_observe(transport, &result, ft8_query_packet(FT8_DEVINFO, 0));
    }

    switch(test) {
        case FT8_TEST_IDENTITY:
            send_observe(transport, &result, ft8_query_packet(FT8_DEVINFO, 0));
            send_observe(transport, &result, ft8_query_packet(FT8_ALLINFO, 0));
            for(unsigned vn = 1; vn <= sources; ++vn)
                send_observe(transport, &result, ft8_query_packet(FT8_MEDIA, vn));
            send_observe(transport, &result, ft8_query_packet(FT8_GETCOND, 0));
            send_observe(transport, &result, ft8_query_packet(FT8_READ, 0));
            break;

        case FT8_TEST_RESET_START:
            /* No media query or AST write before the first start. */
            transport->stage(transport->context, "Reading reset defaults (before first start)");
            send_observe(transport, &result, ft8_query_packet(FT8_GETCOND, 0));
            send_observe(transport, &result, ft8_query_packet(FT8_READ, 0));
            transport->stage(transport->context, "1/2: Start BEFORE media query (1.5s)");
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 1500, true)) goto cleanup;
            transport->stage(transport->context, "Reset active effect; read defaults again");
            send_observe(transport, &result, ft8_query_packet(FT8_RESET, 0));
            if(!wait_observe(transport, &result, 100, false)) goto cleanup;
            send_observe(transport, &result, ft8_query_packet(FT8_DEVINFO, 0));
            /* Read immediately after reset/re-enumeration, before a new start
               can replace the defaults that we are trying to measure. */
            send_observe(transport, &result, ft8_query_packet(FT8_GETCOND, 0));
            send_observe(transport, &result, ft8_query_packet(FT8_READ, 0));
            send_observe(transport, &result, ft8_query_packet(FT8_ALLINFO, 0));
            send_observe(transport, &result, ft8_query_packet(FT8_MEDIA, 1));
            transport->stage(transport->context, "2/2: Start AFTER media query (1.5s)");
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 1500, true)) goto cleanup;
            break;

        case FT8_TEST_LEVELS_POSITIVE:
        case FT8_TEST_LEVELS_NEGATIVE:
            for(unsigned magnitude = 0; magnitude <= 7; ++magnitude) {
                effect.power = test == FT8_TEST_LEVELS_POSITIVE ?
                               (uint8_t)(magnitude << 4) : (uint8_t)magnitude;
                send_observe(transport, &result, ft8_effect_packet(effect));
                if(!wait_observe(transport, &result, 750, true)) goto cleanup;
            }
            break;

        case FT8_TEST_FREQUENCY: {
            const uint8_t frequencies[] = {0x07, 0x13, 0x27, 0x3b, 0x00, 0xff};
            for(size_t i = 0; i < sizeof(frequencies); ++i) {
                effect.frequency = frequencies[i];
                send_observe(transport, &result, ft8_effect_packet(effect));
                if(!wait_observe(transport, &result, 1000, true)) goto cleanup;
            }
            break;
        }

        case FT8_TEST_CONTINUOUS:
            effect.ctrl = 0x10;
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 1000, true)) goto cleanup;
            effect.ctrl = 0x11;
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 5500, true)) goto cleanup;
            break;

        case FT8_TEST_RAMPS_POSITIVE:
        case FT8_TEST_RAMPS_NEGATIVE:
            effect.ctrl = 0x10;
            for(unsigned inc = 1; inc <= 4; inc *= 2) {
                effect.increment = (uint8_t)inc;
                for(unsigned divergence = 0; divergence < 2; ++divergence) {
                    unsigned magnitude = divergence ? 1 : 7;
                    effect.power = (uint8_t)((divergence ? 0x08 : 0x80) |
                        (test == FT8_TEST_RAMPS_POSITIVE ? magnitude << 4 : magnitude));
                    send_observe(transport, &result, ft8_effect_packet(effect));
                    if(!wait_observe(transport, &result, 800 * inc + 300, true))
                        goto cleanup;
                }
            }
            break;

        case FT8_TEST_RAMPS_CONTINUOUS:
            effect.increment = 1;
            for(unsigned i = 0; i < 4; ++i) {
                const uint8_t powers[] = {0xf0, 0x18, 0x87, 0x09};
                effect.power = powers[i];
                send_observe(transport, &result, ft8_effect_packet(effect));
                if(!wait_observe(transport, &result, 2200, true)) goto cleanup;
            }
            break;

        case FT8_TEST_REPLACE_STOP:
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 50, false)) goto cleanup;
            effect.power = 0x03;
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 450, true)) goto cleanup;
            effect.power = 0xf0;
            effect.increment = 4;
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 350, true)) goto cleanup;
            effect.power = 0;
            effect.frequency = 0;
            effect.increment = 0;
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 500, true)) goto cleanup;
            effect.power = 0x70;
            effect.frequency = 0x13;
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 350, false)) goto cleanup;
            effect.power = 0;
            effect.frequency = 0xff;
            effect.increment = 0xff;
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 500, true)) goto cleanup;
            break;

        case FT8_TEST_AST_ROUNDTRIP: {
            const uint8_t times[] = {0, 1, 3, FT8_DEFAULT_AST, 0xff};
            for(size_t i = 0; i < sizeof(times); ++i)
                set_ast(transport, &result, times[i]);
            break;
        }

        case FT8_TEST_AST_EXPIRY: {
            const uint8_t times[] = {0, 3, FT8_DEFAULT_AST};
            for(size_t i = 0; i < sizeof(times); ++i) {
                set_ast(transport, &result, times[i]);
                send_observe(transport, &result, ft8_effect_packet(effect));
                if(!wait_observe(transport, &result, (times[i] + 1u) * 250 + 400, true))
                    goto cleanup;
            }
            break;
        }

        case FT8_TEST_AST_REFRESH:
            set_ast(transport, &result, 1);
            for(unsigned i = 0; i < 6; ++i) {
                send_observe(transport, &result, ft8_effect_packet(effect));
                if(!wait_observe(transport, &result, 300, true)) goto cleanup;
            }
            if(!wait_observe(transport, &result, 700, true)) goto cleanup;
            break;

        case FT8_TEST_AST_POLL:
            set_ast(transport, &result, 1);
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 1800, true)) goto cleanup;
            send_observe(transport, &result, ft8_query_packet(FT8_READ, 0));
            break;

        case FT8_TEST_AST_ACTIVE_CHANGE:
            set_ast(transport, &result, 3);
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 200, false)) goto cleanup;
            set_ast(transport, &result, 0);
            if(!wait_observe(transport, &result, 1100, true)) goto cleanup;
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 650, true)) goto cleanup;
            break;

        case FT8_TEST_SOURCE_MASKS: {
            const uint16_t masks[] = {0x0000, 0x0002, 0x0004, 0x0006, 0x8002, 0xfffe};
            uint8_t times[15];
            set_ast(transport, &result, 3);
            for(size_t m = 0; m < sizeof(masks) / sizeof(masks[0]); ++m) {
                size_t count = 0;
                for(unsigned vn = 1; vn <= 15; ++vn)
                    if(masks[m] & (1u << vn))
                        times[count++] = vn == 1 ? 1 : (uint8_t)(vn + 10);
                send_observe(transport, &result, ft8_ast_packet(masks[m], times, count));
                send_observe(transport, &result, ft8_query_packet(FT8_READ, 0));
            }
            break;
        }

        case FT8_TEST_INVALID_EFFECTS: {
            const ft8_effect_t probes[] = {
                {.ctrl=0x01, .power=0x70, .frequency=0x13}, /* VN=0 */
                {.ctrl=0xf1, .power=0x70, .frequency=0x13}, /* source 15 */
                {.ctrl=0x11, .power=0x77, .frequency=0x13}, /* both peaks */
                {.ctrl=0x11, .power=0xf8, .frequency=0x13, .increment=1},
                {.ctrl=0x11, .power=0xf0, .frequency=0x13, .increment=0},
                {.ctrl=0x11, .power=0x70, .frequency=0x13, .increment=3},
                {.ctrl=0x11, .power=0xf0, .frequency=0x07, .increment=10},
                {.ctrl=0x1f, .power=0x70, .frequency=0x13}, /* reserved CTRL */
                {.ctrl=0x11, .power=0x70, .frequency=0x06},
                {.ctrl=0x11, .power=0x70, .frequency=0x3c}
            };
            for(size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
                send_observe(transport, &result, ft8_effect_packet(effect));
                if(!wait_observe(transport, &result, 150, false)) goto cleanup;
                send_observe(transport, &result, ft8_effect_packet(probes[i]));
                if(!wait_observe(transport, &result, 350, true)) goto cleanup;
                send_observe(transport, &result, ft8_query_packet(FT8_READ, 0));
            }
            break;
        }

        case FT8_TEST_INVALID_PACKETS:
            send_observe(transport, &result, ft8_effect_packet(effect));
            packet = ft8_effect_packet(effect);
            packet.words[0] = 0x01000000u; /* Wrong function: controller. */
            send_observe(transport, &result, packet);
            packet = ft8_effect_packet(effect);
            packet.count = 1;
            send_observe(transport, &result, packet);
            packet.count = 3; /* Extra zero word. */
            send_observe(transport, &result, packet);
            packet.count = 0;
            send_observe(transport, &result, packet);
            packet = ft8_query_packet(FT8_GETCOND, 0);
            packet.count = 0;
            send_observe(transport, &result, packet);
            packet.count = 2;
            send_observe(transport, &result, packet);
            packet = ft8_query_packet(FT8_MEDIA, 1);
            packet.count = 1;
            send_observe(transport, &result, packet);
            send_observe(transport, &result, ft8_query_packet(FT8_MEDIA, 0));
            send_observe(transport, &result, ft8_query_packet(FT8_MEDIA, sources < 15 ? sources + 1 : 0));
            send_observe(transport, &result, ft8_query_packet(FT8_READ, 0x00000100)); /* Phase */
            send_observe(transport, &result, ft8_query_packet(FT8_READ, 0x01000000)); /* Block */
            packet = ft8_ast_packet(0x0002, (const uint8_t[]){0}, 1);
            packet.count = 2; /* Missing ASR/AST. */
            send_observe(transport, &result, packet);
            packet = ft8_ast_packet(0x0006, (const uint8_t[]){0}, 1);
            /* Missing second AST falls in transmitted zero padding. */
            send_observe(transport, &result, packet);
            packet = ft8_ast_packet(0x0002, (const uint8_t[]){0}, 1);
            packet.words[1] = 0x00000100;
            send_observe(transport, &result, packet);
            packet.words[1] = 0x01000000;
            send_observe(transport, &result, packet);
            send_observe(transport, &result, ft8_query_packet(FT8_GETCOND, 0));
            send_observe(transport, &result, ft8_query_packet(FT8_READ, 0));
            break;

        case FT8_TEST_WAVEFORM:
            send_observe(transport, &result, ft8_query_packet(FT8_MEDIA, 1));
            send_observe(transport, &result, ft8_query_packet(FT8_READ, 1));
            packet = (ft8_packet_t){.command=FT8_WRITE, .count=7};
            packet.words[0] = FT8_FUNCTION;
            packet.words[1] = 1;
            /* Twenty 10ms steps. WFE terminates; bit 7 of first step repeats. */
            for(unsigned i = 0; i < 20; ++i) {
                uint8_t step = i < 10 ? 7 : 0x0b;
                if(i == 19) step = 0x40;
                packet.words[2 + i / 4] |= (uint32_t)step << (8 * (i % 4));
            }
            send_observe(transport, &result, packet);
            if(!wait_observe(transport, &result, 600, true)) goto cleanup;
            send_observe(transport, &result, ft8_query_packet(FT8_READ, 1));
            packet.words[2] |= 0x80;
            send_observe(transport, &result, packet);
            if(!wait_observe(transport, &result, 1200, true)) goto cleanup;
            break;

        case FT8_TEST_UNKNOWN_RETRY:
            send_observe(transport, &result, ft8_query_packet(FT8_GETCOND, 0));
            send_observe(transport, &result, ft8_query_packet(FT8_RETRY, 0));
            packet = ft8_query_packet(FT8_READ, 0);
            packet.command = 0x0d;
            send_observe(transport, &result, packet);
            packet.command = 0x7f;
            packet.count = 0;
            send_observe(transport, &result, packet);
            break;

        case FT8_TEST_KILL:
            send_observe(transport, &result, ft8_effect_packet(effect));
            if(!wait_observe(transport, &result, 500, true)) goto cleanup;
            send_observe(transport, &result, ft8_query_packet(FT8_KILL, 0));
            if(!wait_observe(transport, &result, 100, false))
                return result; /* Already killed: do not send cleanup writes. */
            send_observe(transport, &result, ft8_query_packet(FT8_DEVINFO, 0));
            return result;

        case FT8_TEST_COUNT:
            break;
    }

cleanup:
    /* Ignore cancellation for cleanup; an unresponsive pack still gets a
       best-effort stop. Neither no response nor a protocol error aborts probes. */
    packet = ft8_effect_packet((ft8_effect_t){.ctrl=0x10, .frequency=0x13});
    send_observe(transport, &result, packet);
    packet = ft8_ast_packet(0x0002, (const uint8_t[]){FT8_DEFAULT_AST}, 1);
    send_observe(transport, &result, packet);
    return result;
}
