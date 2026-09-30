#include "ft4_tests.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t command;
    size_t count;
    uint32_t words[2];
} request_t;

typedef struct {
    request_t requests[256];
    unsigned count;
    unsigned waits;
    unsigned cancel_after;
    unsigned fail_at;
} capture_t;

static bool send(void *context, uint8_t command, const uint32_t *words,
                 size_t count) {
    capture_t *capture = context;
    request_t *request;

    assert(capture->count < sizeof(capture->requests) / sizeof(capture->requests[0]));
    request = &capture->requests[capture->count++];
    assert(count <= 2);
    request->command = command;
    request->count = count;
    if(count)
        memcpy(request->words, words, count * sizeof(*words));
    return capture->count != capture->fail_at;
}

static void wait_frame(void *context) {
    capture_t *capture = context;

    ++capture->waits;
}

static bool cancelled(void *context) {
    capture_t *capture = context;

    return capture->cancel_after && capture->count >= capture->cancel_after;
}

static bool run(capture_t *capture, ft4_test_t test, bool low_rate) {
    const ft4_transport_t transport = {
        .send = send, .wait_frame = wait_frame,
        .cancelled = cancelled, .context = capture
    };

    return ft4_test_run(test, low_rate, &transport);
}

static bool contains(const capture_t *capture, uint32_t control_word) {
    for(unsigned i = 0; i < capture->count; ++i) {
        const request_t *request = &capture->requests[i];

        if(request->command == 0x0f && request->count == 2 &&
           request->words[0] == 0x10000000 &&
           request->words[1] == control_word)
            return true;
    }
    return false;
}

static void stopped(const capture_t *capture, uint32_t mode, uint32_t gain) {
    assert(capture->requests[capture->count - 2].words[1] == (mode << 8 | 2));
    assert(capture->requests[capture->count - 1].words[1] == (gain << 8 | 1));
}

static void expect_control(const capture_t *capture, unsigned *index,
                           uint8_t subcommand, uint8_t parameter) {
    const request_t *request;

    assert(*index < capture->count);
    request = &capture->requests[(*index)++];
    assert(request->command == 0x0f && request->count == 2);
    assert(request->words[0] == 0x10000000u);
    assert(request->words[1] == ((uint32_t)parameter << 8 | subcommand));
}

static void check_transitions(bool low_rate) {
    capture_t capture = {0};
    unsigned index = 2;
    uint8_t rate = low_rate ? 4 : 0;

    assert(run(&capture, FT4_TEST_FORMAT_TRANSITIONS, low_rate));
    assert(capture.requests[0].command == 3);
    assert(capture.requests[1].command == 1);
    expect_control(&capture, &index, 3, 15);
    expect_control(&capture, &index, 4, 0);
    for(unsigned session = 0; session < 2; ++session) {
        for(unsigned phase = 0; phase < 3; ++phase) {
            expect_control(&capture, &index, 2,
                           0x80 | rate | ((session + phase) & 1u));
            for(unsigned i = 0; i < 20; ++i)
                expect_control(&capture, &index, 1, 15);
        }
        expect_control(&capture, &index, 2, 0);
        expect_control(&capture, &index, 1, 15);
    }
    assert(index == capture.count && capture.waits == 0);

    memset(&capture, 0, sizeof(capture));
    index = 2;
    assert(run(&capture, FT4_TEST_EXTU_TRANSITIONS, low_rate));
    expect_control(&capture, &index, 3, 15);
    expect_control(&capture, &index, 4, 0);
    expect_control(&capture, &index, 2, 0x80 | rate);
    for(unsigned phase = 0; phase < 4; ++phase) {
        if(phase)
            expect_control(&capture, &index, 4, phase == 3 ? 0 : phase);
        for(unsigned i = 0; i < 30; ++i)
            expect_control(&capture, &index, 1, 15);
    }
    expect_control(&capture, &index, 2, 0);
    expect_control(&capture, &index, 1, 15);
    assert(index == capture.count && capture.waits == 0);

    /* Cancel or lose a response during either session/active EXTU change.
       Cleanup cannot rely on which attempted format the ASIC accepted. */
    for(unsigned test = FT4_TEST_FORMAT_TRANSITIONS;
        test <= FT4_TEST_EXTU_TRANSITIONS; ++test) {
        const unsigned points[] = {5, 26, 70, 100};

        for(unsigned i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
            memset(&capture, 0, sizeof(capture));
            capture.cancel_after = points[i];
            assert(!run(&capture, (ft4_test_t)test, low_rate));
            stopped(&capture, 0, 15);
            memset(&capture, 0, sizeof(capture));
            capture.fail_at = points[i];
            assert(!run(&capture, (ft4_test_t)test, low_rate));
            stopped(&capture, 0, 15);
        }
    }
}

int main(void) {
    capture_t capture = {0};

    assert(run(&capture, FT4_TEST_IDENTITY, true));
    assert(capture.count == 3);
    assert(capture.requests[0].command == 3);
    assert(capture.requests[1].command == 1);
    assert(capture.requests[2].command == 2);
    assert(capture.requests[2].count == 0);

    memset(&capture, 0, sizeof(capture));
    assert(run(&capture, FT4_TEST_PCM_COPY, true));
    assert(contains(&capture, 0x00000104)); /* EXTU copy */
    assert(contains(&capture, 0x00008402)); /* low-rate linear start */
    assert(capture.waits == 0);            /* no extra frame between reads */
    stopped(&capture, 4, 15);

    memset(&capture, 0, sizeof(capture));
    assert(run(&capture, FT4_TEST_PCM_TEN, false));
    assert(contains(&capture, 0x00000204));
    assert(contains(&capture, 0x00008002));
    stopped(&capture, 0, 15);

    memset(&capture, 0, sizeof(capture));
    assert(run(&capture, FT4_TEST_ULAW, true));
    assert(contains(&capture, 0x00008502));
    stopped(&capture, 5, 15);

    memset(&capture, 0, sizeof(capture));
    assert(run(&capture, FT4_TEST_ULAW, false));
    assert(contains(&capture, 0x00008102));
    stopped(&capture, 1, 15);

    memset(&capture, 0, sizeof(capture));
    assert(run(&capture, FT4_TEST_ALIEN_FRONT, false));
    assert(contains(&capture, 0x00001f03));
    assert(contains(&capture, 0x00000005));
    assert(contains(&capture, 0x00008402)); /* retail probe always low-rate */
    assert(contains(&capture, 0x000000fc)); /* FT4 subcommand, not command FC */
    stopped(&capture, 4, 31);

    memset(&capture, 0, sizeof(capture));
    assert(run(&capture, FT4_TEST_VOLUME_12, true));
    assert(contains(&capture, 0x00000105));

    memset(&capture, 0, sizeof(capture));
    assert(run(&capture, FT4_TEST_DIAGNOSTIC_STOPPED, true));
    assert(capture.count == 3);
    assert(contains(&capture, 0x000000fc));
    assert(!contains(&capture, 0x00008402));

    memset(&capture, 0, sizeof(capture));
    assert(run(&capture, FT4_TEST_OVERFLOW, true));
    assert(capture.waits == 8);
    stopped(&capture, 4, 15);

    memset(&capture, 0, sizeof(capture));
    capture.cancel_after = 7;
    assert(!run(&capture, FT4_TEST_PCM_ZERO, true));
    stopped(&capture, 4, 15);

    memset(&capture, 0, sizeof(capture));
    capture.fail_at = 7;
    assert(!run(&capture, FT4_TEST_PCM_ZERO, true));
    stopped(&capture, 4, 15);

    check_transitions(false);
    check_transitions(true);
    puts("FT4 packet sequences passed");
    return 0;
}
