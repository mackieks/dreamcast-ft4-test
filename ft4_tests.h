#ifndef FT4_TESTS_H
#define FT4_TESTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool (*send)(void *context, uint8_t command, const uint32_t *words,
                 size_t count);
    void (*wait_frame)(void *context);
    bool (*cancelled)(void *context);
    void *context;
} ft4_transport_t;

typedef enum {
    FT4_TEST_IDENTITY,
    FT4_TEST_PCM_ZERO,
    FT4_TEST_PCM_COPY,
    FT4_TEST_PCM_TEN,
    FT4_TEST_ULAW,
    FT4_TEST_VOLUME_30,
    FT4_TEST_VOLUME_12,
    FT4_TEST_DIAGNOSTIC_STOPPED,
    FT4_TEST_DIAGNOSTIC_SAMPLING,
    FT4_TEST_PLANET_RING,
    FT4_TEST_ALIEN_FRONT,
    FT4_TEST_OVERFLOW,
    FT4_TEST_FORMAT_TRANSITIONS,
    FT4_TEST_EXTU_TRANSITIONS,
    FT4_TEST_COUNT
} ft4_test_t;

extern const char *const ft4_test_names[FT4_TEST_COUNT];

/* Unknown-command replies are observations, not test failures. */
bool ft4_test_run(ft4_test_t test, bool low_rate,
                  const ft4_transport_t *transport);

#endif
