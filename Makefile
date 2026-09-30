TARGET = ft4-test.elf
OBJS = main.o ft4_tests.o

all: $(TARGET) ft4-test.bin

include $(KOS_BASE)/Makefile.rules

KOS_CFLAGS += -std=gnu11 -O2 -Wall -Wextra

$(TARGET): $(OBJS)
	$(KOS_CC) $(KOS_CFLAGS) $(KOS_LDFLAGS) -o $@ $(OBJS) $(KOS_LIBS)

ft4-test.bin: $(TARGET)
	$(KOS_OBJCOPY) -O binary $< $@

clean:
	rm -f $(OBJS) $(TARGET) ft4-test.bin

.PHONY: all clean
