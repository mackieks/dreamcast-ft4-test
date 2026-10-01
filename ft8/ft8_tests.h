#ifndef FT8_TESTS_H
#define FT8_TESTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FT8_FUNCTION 0x00010000u
#define FT8_MAX_WORDS 8u
#define FT8_DEFAULT_AST 0x13u

enum {
    FT8_DEVINFO = 0x01, FT8_ALLINFO = 0x02, FT8_RESET = 0x03,
    FT8_KILL = 0x04, FT8_GETCOND = 0x09, FT8_MEDIA = 0x0a,
    FT8_READ = 0x0b, FT8_WRITE = 0x0c, FT8_SETCOND = 0x0e,
    FT8_RETRY = 0xfc
};

typedef struct {
    uint8_t command;
    size_t count;
    uint32_t words[FT8_MAX_WORDS];
} ft8_packet_t;

typedef struct {
    uint8_t ctrl;
    uint8_t power;
    uint8_t frequency;
    uint8_t increment;
} ft8_effect_t;

/* Return the response byte (including errors), or -1 for no response.
   Timing is milliseconds, independent of PAL/NTSC video cadence. */
typedef struct {
    int (*send)(void *context, const ft8_packet_t *packet);
    uint32_t (*now_ms)(void *context);
    void (*wait_ms)(void *context, unsigned milliseconds);
    bool (*cancelled)(void *context);
    void (*stage)(void *context, const char *label);
    void *context;
    unsigned sources;
} ft8_transport_t;

typedef enum {
    FT8_TEST_IDENTITY,
    FT8_TEST_RESET_START,
    FT8_TEST_LEVELS_POSITIVE,
    FT8_TEST_LEVELS_NEGATIVE,
    FT8_TEST_FREQUENCY,
    FT8_TEST_CONTINUOUS,
    FT8_TEST_RAMPS_POSITIVE,
    FT8_TEST_RAMPS_NEGATIVE,
    FT8_TEST_RAMPS_CONTINUOUS,
    FT8_TEST_REPLACE_STOP,
    FT8_TEST_AST_ROUNDTRIP,
    FT8_TEST_AST_EXPIRY,
    FT8_TEST_AST_REFRESH,
    FT8_TEST_AST_POLL,
    FT8_TEST_AST_ACTIVE_CHANGE,
    FT8_TEST_SOURCE_MASKS,
    FT8_TEST_INVALID_EFFECTS,
    FT8_TEST_INVALID_PACKETS,
    FT8_TEST_WAVEFORM,
    FT8_TEST_UNKNOWN_RETRY,
    FT8_TEST_KILL,
    FT8_TEST_COUNT
} ft8_test_t;

typedef struct {
    unsigned requests;
    unsigned errors;
    unsigned missing;
    bool cancelled;
} ft8_result_t;

extern const char *const ft8_test_names[FT8_TEST_COUNT];

uint32_t ft8_host_word(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3);
ft8_packet_t ft8_effect_packet(ft8_effect_t effect);
ft8_packet_t ft8_ast_packet(uint16_t mask, const uint8_t *times, size_t count);
ft8_packet_t ft8_query_packet(uint8_t command, uint32_t address);
void ft8_stop_restore(const ft8_transport_t *transport);
ft8_result_t ft8_test_run(ft8_test_t test, const ft8_transport_t *transport);

#endif
