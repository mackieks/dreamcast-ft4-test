#include "ft4_tests.h"

/* Ft4_100e.pdf: host-memory words, before Maple's word reversal. */
#define MIC_FUNCTION 0x10000000u
#define COMMAND_INFO 0x01u
#define COMMAND_ALL_INFO 0x02u
#define COMMAND_RESET 0x03u
#define COMMAND_MIC 0x0fu
#define GET_SAMPLES 0x01u
#define BASIC_CONTROL 0x02u
#define AMP_CONTROL 0x03u
#define EXTU_BIT 0x04u
#define VOLUME_MODE 0x05u
#define TEST_MODE 0xfcu
#define DEFAULT_GAIN 0x0fu
#define LOW_RATE 0x04u
#define ULAW 0x01u
#define START_SAMPLING 0x80u

const char *const ft4_test_names[FT4_TEST_COUNT] = {
    "Device info / AllInfo",
    "Linear PCM, EXTU zero",
    "Linear PCM, EXTU copy",
    "Linear PCM, EXTU binary 10",
    "8-bit mu-law",
    "Volume_Mode +30 dB",
    "Volume_Mode +12 dB",
    "Test_Mode while stopped",
    "Test_Mode while sampling",
    "Planet Ring command probe",
    "Alien Front command probe",
    "Sample-buffer overflow",
    "PCM / mu-law transitions",
    "EXTU while sampling"
};

static bool control(const ft4_transport_t *transport, uint8_t subcommand,
                    uint8_t parameter) {
    const uint32_t words[2] = {
        MIC_FUNCTION, (uint32_t)subcommand | ((uint32_t)parameter << 8)
    };

    return transport->send(transport->context, COMMAND_MIC, words, 2);
}

static bool reset(const ft4_transport_t *transport) {
    return transport->send(transport->context, COMMAND_RESET, NULL, 0) &&
           transport->send(transport->context, COMMAND_INFO, NULL, 0);
}

static bool read_samples(const ft4_transport_t *transport, unsigned count) {
    for(unsigned i = 0; i < count; ++i) {
        if(transport->cancelled(transport->context) ||
           !control(transport, GET_SAMPLES, DEFAULT_GAIN))
            return false;
    }
    return true;
}

static bool stop_sampling(const ft4_transport_t *transport) {
    /* KOS's SIP driver also uses DT1=00 to stop, regardless of current format.
       This restores high-rate PCM while stopped and avoids assuming that an
       attempted active format change was accepted. */
    if(!control(transport, BASIC_CONTROL, 0))
        return false;
    return control(transport, GET_SAMPLES, DEFAULT_GAIN);
}

static bool transitions(ft4_test_t test, uint8_t rate,
                        const ft4_transport_t *transport) {
    bool started = false;
    bool success = false;

    if(!control(transport, EXTU_BIT, 0))
        return false;

    if(test == FT4_TEST_FORMAT_TRANSITIONS) {
        /* Independently start in each format: rejection of PCM -> mu-law
           must not prevent testing mu-law -> PCM from actual mu-law input.
           No stop/reset occurs between the three phases of either session. */
        for(unsigned session = 0; session < 2; ++session) {
            for(unsigned phase = 0; phase < 3; ++phase) {
                uint8_t format = ((session + phase) & 1u) ? ULAW : 0;

                if(transport->cancelled(transport->context))
                    goto cleanup;
                started = true;
                if(!control(transport, BASIC_CONTROL,
                            rate | format | START_SAMPLING) ||
                   !read_samples(transport, 20))
                    goto cleanup;
            }
            if(!stop_sampling(transport))
                return false;
            started = false;
        }
    }
    else {
        const uint8_t expansions[] = {0, 1, 2, 0};

        started = true;
        if(!control(transport, BASIC_CONTROL, rate | START_SAMPLING))
            goto cleanup;
        for(unsigned phase = 0; phase < 4; ++phase) {
            if(transport->cancelled(transport->context))
                goto cleanup;
            if(phase && !control(transport, EXTU_BIT, expansions[phase]))
                goto cleanup;
            if(!read_samples(transport, 30))
                goto cleanup;
        }
    }
    success = true;

cleanup:
    if(started && !stop_sampling(transport))
        return false;
    return success;
}

bool ft4_test_run(ft4_test_t test, bool low_rate,
                  const ft4_transport_t *transport) {
    uint8_t mode = low_rate ? LOW_RATE : 0;
    uint8_t gain = DEFAULT_GAIN;
    bool started = false;
    bool success = false;

    if(test >= FT4_TEST_COUNT || !reset(transport))
        return false;

    if(test == FT4_TEST_IDENTITY)
        return transport->send(transport->context, COMMAND_ALL_INFO, NULL, 0);

    if(test == FT4_TEST_DIAGNOSTIC_STOPPED)
        return control(transport, TEST_MODE, 0);

    /* These probe the observed command families. Test_Mode's unlogged
       parameters use the SIP specification's zeros; this is not a byte-exact
       replay of either retail game's complete request stream. */
    if(test == FT4_TEST_PLANET_RING || test == FT4_TEST_ALIEN_FRONT) {
        mode = LOW_RATE;
        gain = test == FT4_TEST_PLANET_RING ? 0x1eu : 0x1fu;
    }
    else if(test == FT4_TEST_ULAW)
        mode |= ULAW;

    if(!control(transport, AMP_CONTROL, gain))
        return false;

    if(test == FT4_TEST_FORMAT_TRANSITIONS || test == FT4_TEST_EXTU_TRANSITIONS)
        return transitions(test, mode, transport);

    if(test >= FT4_TEST_PCM_ZERO && test <= FT4_TEST_PCM_TEN) {
        uint8_t expansion = (uint8_t)(test - FT4_TEST_PCM_ZERO);

        if(!control(transport, EXTU_BIT, expansion))
            return false;
    }
    else if(test == FT4_TEST_VOLUME_30 || test == FT4_TEST_VOLUME_12 ||
            test == FT4_TEST_ALIEN_FRONT) {
        uint8_t reference = test == FT4_TEST_VOLUME_12 ? 1 : 0;

        /* A stock 0x0f-capability mic should return Command Unknown here.
           Continue so its subsequent sampling behavior is visible on the LA. */
        if(!control(transport, VOLUME_MODE, reference))
            return false;
    }

    if(!control(transport, BASIC_CONTROL, mode | START_SAMPLING))
        return false;
    started = true;

    if(test == FT4_TEST_OVERFLOW) {
        /* Eight display frames exceed the stock buffer's capacity at either
           PAL or NTSC, and at either sampling rate. */
        for(unsigned i = 0; i < 8; ++i) {
            if(transport->cancelled(transport->context))
                goto cleanup;
            transport->wait_frame(transport->context);
        }
    }

    /* KOS dispatches each queued request at the next display-frame Maple DMA.
       Queue the next read immediately on completion; an extra wait here would
       halve the read rate and overflow the stock buffer. Diagnostic commands
       intentionally interrupt this cadence. This is not a timing replay. */
    for(unsigned i = 0; i < 120; ++i) {
        if(transport->cancelled(transport->context))
            goto cleanup;

        if((test == FT4_TEST_DIAGNOSTIC_SAMPLING ||
            test == FT4_TEST_PLANET_RING || test == FT4_TEST_ALIEN_FRONT) &&
           (i == 3 || i == 30 || i == 60)) {
            if(!control(transport, TEST_MODE, 0))
                goto cleanup;
        }

        if(!control(transport, GET_SAMPLES, gain))
            goto cleanup;
    }
    success = true;

cleanup:
    if(started) {
        /* Also stop on cancel or failure. Never leave a test sampling. */
        if(!control(transport, BASIC_CONTROL, mode))
            return false;
        if(!control(transport, GET_SAMPLES, gain))
            return false;
    }
    return success;
}
