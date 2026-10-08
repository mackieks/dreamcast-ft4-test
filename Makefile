TARGET = ft4-test.elf
OBJS = main.o ft4_tests.o

all: $(TARGET) ft4-test.bin ft8-test.elf ft8-test.bin ft0-test.elf ft0-test.bin

include $(KOS_BASE)/Makefile.rules

CFLAGS += -std=gnu11 -O2 -Wall -Wextra

$(TARGET): $(OBJS)
	kos-cc $(CFLAGS) -o $@ $(OBJS)

ft4-test.bin: $(TARGET)
	$(KOS_OBJCOPY) -O binary $< $@

FT8_OBJS = ft8/main.o ft8/ft8_tests.o ft8/editor.o

ft8-test.elf: $(FT8_OBJS)
	kos-cc $(CFLAGS) -o $@ $(FT8_OBJS)

ft8-test.bin: ft8-test.elf
	$(KOS_OBJCOPY) -O binary $< $@

FT0_OBJS = ft0/main.o ft0/history.o

ft0-test.elf: $(FT0_OBJS)
	kos-cc $(CFLAGS) -o $@ $(FT0_OBJS) -lm

ft0-test.bin: ft0-test.elf
	$(KOS_OBJCOPY) -O binary $< $@

clean:
	rm -f $(OBJS) $(FT8_OBJS) $(FT0_OBJS) $(TARGET) ft4-test.bin ft8-test.elf ft8-test.bin ft0-test.elf ft0-test.bin

.PHONY: all clean
