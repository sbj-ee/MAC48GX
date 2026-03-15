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

# Test build — no SDL2 dependency, uses test_harness.c instead of main_sdl.c
TEST_SRCS := $(CORE_SRCS) $(MAC)/x48_sdl.c $(MAC)/lcd_mac.c $(MAC)/test_harness.c
TEST_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(TEST_SRCS)))

TEST_CFLAGS := -std=gnu11 -O2 \
               -I$(MAC) -I$(JNI) \
               -DMAC_BUILD=1 -DLINUX=1 -DSYSV_TIME=1 \
               -Wno-implicit-function-declaration \
               -Wno-incompatible-pointer-types \
               -Wno-int-conversion \
               -Wno-unused-variable \
               -Wno-unused-function \
               -Wno-deprecated-non-prototype

TEST_TARGET := $(BUILD)/droid48-test

APP_NAME   := HP-48GX.app
APP_DIR    := $(BUILD)/$(APP_NAME)

.PHONY: all clean run test bundle

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

# Test target — compile test_harness.c with no SDL flags
$(BUILD)/test_harness.o: $(MAC)/test_harness.c
	$(CC) $(TEST_CFLAGS) -c $< -o $@

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(TEST_OBJS) -lpthread -o $@
	@echo "--- Built: $@"
	@cp -n $(JNI)/rom  $(BUILD)/rom  2>/dev/null || true
	@cp -n $(JNI)/ram  $(BUILD)/ram  2>/dev/null || true

test: $(BUILD) $(TEST_TARGET)
	$(TEST_TARGET)

# macOS .app bundle — double-clickable, no terminal window
bundle: all
	@echo "--- Creating $(APP_NAME) bundle ---"
	@mkdir -p "$(APP_DIR)/Contents/MacOS"
	@mkdir -p "$(APP_DIR)/Contents/Resources"
	@cp $(TARGET) "$(APP_DIR)/Contents/MacOS/droid48-mac"
	@cp $(MAC)/Info.plist "$(APP_DIR)/Contents/"
	@cp $(MAC)/HP48GX.icns "$(APP_DIR)/Contents/Resources/" 2>/dev/null || true
	@cp -n $(JNI)/rom "$(APP_DIR)/Contents/Resources/rom" 2>/dev/null || true
	@cp -n $(JNI)/ram "$(APP_DIR)/Contents/Resources/ram" 2>/dev/null || true
	@test -f $(JNI)/hp48 && cp -n $(JNI)/hp48 "$(APP_DIR)/Contents/Resources/hp48" 2>/dev/null || true
	@echo "--- Built: $(APP_DIR)"
	@echo "    Open with: open $(APP_DIR)"
	@echo "    Or drag to /Applications"

clean:
	rm -rf $(BUILD)
