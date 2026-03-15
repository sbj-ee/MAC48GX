# Makefile for droid48-mac
# Usage: make -f Makefile  (from the macos/ directory)

JNI  := ../app/src/main/jni
MAC  := .
BUILD := build

CC      := clang
CFLAGS  := -std=gnu11 -O2 \
           -I$(MAC) -I$(JNI) \
           -DMAC_BUILD=1 -DLINUX=1 -DSYSV_TIME=1 \
           $(shell sdl2-config --cflags) \
           -Wno-implicit-function-declaration \
           -Wno-incompatible-pointer-types \
           -Wno-int-conversion \
           -Wno-unused-variable \
           -Wno-unused-function \
           -Wno-deprecated-non-prototype

LDFLAGS := $(shell sdl2-config --libs) -lSDL2_ttf -lpthread

CORE_SRCS := \
    $(JNI)/actions.c \
    $(JNI)/binio.c \
    $(JNI)/debugger.c \
    $(JNI)/device.c \
    $(JNI)/disasm.c \
    $(JNI)/emulate.c \
    $(JNI)/errors.c \
    $(JNI)/init.c \
    $(JNI)/memory.c \
    $(JNI)/options.c \
    $(JNI)/register.c \
    $(JNI)/resources.c \
    $(JNI)/romio.c \
    $(JNI)/rpl.c \
    $(JNI)/serial.c \
    $(JNI)/timer.c

MAC_SRCS := \
    $(MAC)/main_sdl.c \
    $(MAC)/x48_sdl.c \
    $(MAC)/lcd_mac.c

SRCS := $(CORE_SRCS) $(MAC_SRCS)
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRCS)))

TARGET := $(BUILD)/droid48-mac

.PHONY: all clean run

all: $(BUILD) $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

# Generic rule: compile any .c → build/
$(BUILD)/%.o: $(JNI)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(MAC)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	@echo "--- Built: $@"
	@# Copy data files next to binary so it can find them
	@cp -n $(JNI)/rom  $(BUILD)/rom  2>/dev/null || true
	@cp -n $(JNI)/ram  $(BUILD)/ram  2>/dev/null || true
	@test -f $(JNI)/hp48 && cp -n $(JNI)/hp48 $(BUILD)/hp48 2>/dev/null || true

run: all
	$(TARGET)

clean:
	rm -rf $(BUILD)
