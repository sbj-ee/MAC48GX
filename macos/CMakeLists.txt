cmake_minimum_required(VERSION 3.10)
project(droid48-mac C)

set(CMAKE_C_STANDARD 99)

# SDL2
find_package(SDL2 REQUIRED)
if(NOT SDL2_FOUND)
    # Fallback: use sdl2-config
    execute_process(COMMAND sdl2-config --cflags OUTPUT_VARIABLE SDL2_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND sdl2-config --libs   OUTPUT_VARIABLE SDL2_LIBS   OUTPUT_STRIP_TRAILING_WHITESPACE)
    separate_arguments(SDL2_CFLAGS)
    separate_arguments(SDL2_LIBS)
else()
    set(SDL2_CFLAGS "")
    set(SDL2_LIBS   SDL2::SDL2)
endif()

set(JNI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/jni)
set(MAC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Emulator core sources (from jni/, no Android-specific files)
set(CORE_SOURCES
    ${JNI_DIR}/actions.c
    ${JNI_DIR}/binio.c
    ${JNI_DIR}/debugger.c
    ${JNI_DIR}/device.c
    ${JNI_DIR}/disasm.c
    ${JNI_DIR}/emulate.c
    ${JNI_DIR}/errors.c
    ${JNI_DIR}/init.c
    ${JNI_DIR}/memory.c
    ${JNI_DIR}/options.c
    ${JNI_DIR}/register.c
    ${JNI_DIR}/resources.c
    ${JNI_DIR}/romio.c
    ${JNI_DIR}/rpl.c
    ${JNI_DIR}/serial.c
    ${JNI_DIR}/timer.c
)

# macOS-specific sources (replace main.c, x48.c, lcd.c)
set(MAC_SOURCES
    ${MAC_DIR}/main_sdl.c
    ${MAC_DIR}/x48_sdl.c
    ${MAC_DIR}/lcd_mac.c
)

add_executable(droid48-mac ${CORE_SOURCES} ${MAC_SOURCES})

# Include: macos/ first so our x48.h overrides jni/x48.h
target_include_directories(droid48-mac PRIVATE
    ${MAC_DIR}
    ${JNI_DIR}
)

target_compile_definitions(droid48-mac PRIVATE
    MAC_BUILD=1
    LINUX=1          # use Linux-style time
    SYSV_TIME=1
)

# SDL2
if(SDL2_FOUND)
    target_link_libraries(droid48-mac SDL2::SDL2 pthread)
else()
    target_compile_options(droid48-mac PRIVATE ${SDL2_CFLAGS})
    target_link_libraries(droid48-mac ${SDL2_LIBS} pthread)
endif()

# Suppress warnings from old C code
target_compile_options(droid48-mac PRIVATE
    -Wno-implicit-function-declaration
    -Wno-incompatible-pointer-types
    -Wno-int-conversion
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-deprecated-non-prototype
)

# Copy ROM/RAM to build dir so the binary can find them
configure_file(${JNI_DIR}/rom  ${CMAKE_CURRENT_BINARY_DIR}/rom  COPYONLY)
configure_file(${JNI_DIR}/ram  ${CMAKE_CURRENT_BINARY_DIR}/ram  COPYONLY)
if(EXISTS ${JNI_DIR}/hp48)
    configure_file(${JNI_DIR}/hp48 ${CMAKE_CURRENT_BINARY_DIR}/hp48 COPYONLY)
endif()
