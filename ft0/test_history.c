#include "history.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    ft0_clear_history(128, 128);
    for(unsigned i = 0; i < 1000; ++i)
        ft0_record_position(128, 128);
    assert(stick_history.count == 0);

    ft0_record_position(131, 132);
    assert(stick_history.count == 1);
    assert(stick_history.points[0].color == ft0_distance_color(5.0f));
    stick_history_t snapshot = stick_history;
    for(unsigned i = 0; i < 1000; ++i)
        ft0_record_position(131, 132);
    assert(memcmp(&snapshot, &stick_history, sizeof(snapshot)) == 0);

    // wrap more than twice, then check every surviving coordinate in FIFO order
    ft0_clear_history(255, 255);
    for(unsigned i = 0; i < 1300; ++i)
        ft0_record_position((uint8_t)i, (uint8_t)(i / 256));
    assert(stick_history.count == FT0_HISTORY_CAPACITY);
    for(unsigned i = 0; i < FT0_HISTORY_CAPACITY; ++i) {
        const unsigned sequence = 1300 - FT0_HISTORY_CAPACITY + i;
        const stick_point_t *point =
            &stick_history.points[(stick_history.next + i) % FT0_HISTORY_CAPACITY];
        assert(point->x == (uint8_t)sequence);
        assert(point->y == (uint8_t)(sequence / 256));
    }
    assert(stick_history.min_x == 0 && stick_history.max_x == 255);
    assert(stick_history.min_y == 0 && stick_history.max_y == 5);

    // clear from a deflected position without repopulating the trail while held still
    ft0_clear_history(255, 0);
    ft0_record_position(255, 0);
    assert(stick_history.count == 0 && stick_history.next == 0);
    ft0_record_position(254, 1);
    assert(stick_history.count == 1);
    assert(stick_history.min_x == 254 && stick_history.max_x == 254);
    assert(stick_history.min_y == 1 && stick_history.max_y == 1);

    assert(ft0_distance_color(0) == 0x07e0);
    assert(ft0_distance_color(16) == 0xffe0);
    assert(ft0_distance_color(32) == 0xf800);
    assert(ft0_distance_color(361) == 0xf800);
    unsigned changes = 0;
    uint16_t previous = ft0_distance_color(0);
    for(unsigned i = 1; i <= 128; ++i) {
        const uint16_t color = ft0_distance_color((float)i / 4.0f);
        assert((color >> 11) >= (previous >> 11));
        assert(((color >> 5) & 63) <= ((previous >> 5) & 63));
        assert((color & 31) == 0);
        if(color != previous) ++changes;
        previous = color;
    }
    assert(changes > 60);
    puts("FT0 motion-only FIFO, clear, persistent ranges and continuous hue passed");
    return 0;
}
