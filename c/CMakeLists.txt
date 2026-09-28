cmake_minimum_required(VERSION 3.10)
project(bomber C)

find_program(ZCC_EXECUTABLE z88dk.zcc)
if(NOT ZCC_EXECUTABLE)
    message(FATAL_ERROR "z88dk.zcc not found (snap install z88dk)")
endif()

# Platform to build (platform/<name>); the core is the same for all.
set(PLATFORM "mz" CACHE STRING "target platform")
set(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/core")
set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")
set(PLAT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/platform/${PLATFORM}")

if(PLATFORM STREQUAL "mz")
    # Same toolchain and flags as MZPico-800-Manager (z88dk sccz80, Sharp MZ target).
    set(TARGET_FLAGS +mz -lm -create-app -pragma-define:REGISTER_SP=0xd000)
    set(PLAT_SOURCES "${PLAT_DIR}/plat_mz.c" "${PLAT_DIR}/uc_mz.c" "${PLAT_DIR}/tables.c"
                     "${COMMON_DIR}/netdev_card.c" "${COMMON_DIR}/z80_loops.c")
    set(APP_SUFFIX mzt)
    set(OUT_SUFFIX mzf)
elseif(PLATFORM STREQUAL "zx")
    # ZX Spectrum 48K: code from 24000 so that it fits under 65536, stack in the UDG area.
    set(TARGET_FLAGS +zx -lndos -create-app -zorg=24000 -pragma-define:REGISTER_SP=65535
                     -pragma-define:CLIB_EXIT_STACK_SIZE=0)
    set(PLAT_SOURCES "${PLAT_DIR}/plat_zx.c" "${PLAT_DIR}/tables.c"
                     "${COMMON_DIR}/netdev_none.c" "${COMMON_DIR}/z80_loops.c")
    set(APP_SUFFIX tap)
    set(OUT_SUFFIX tap)
else()
    message(FATAL_ERROR "unknown PLATFORM ${PLATFORM}")
endif()

set(COMMON_FLAGS ${TARGET_FLAGS} --list -m -I${CORE_DIR} -I${COMMON_DIR} -I${PLAT_DIR})

# Link order is memory order. On the Spectrum the first 8 KB land in contended
# RAM (slower while the picture is drawn): menus and lobby go there, the frame
# loop's code and the platform's screen routine last.
set(BOMBER_SOURCES
    "${CORE_DIR}/bomber.c"
    "${CORE_DIR}/netplay.c"
    "${CORE_DIR}/data.c"
    "${CORE_DIR}/map.c"
    "${CORE_DIR}/game.c"
    "${CORE_DIR}/input.c"
    "${CORE_DIR}/video.c"
    "${CORE_DIR}/enemy.c"
    "${CORE_DIR}/bomb.c"
    "${CORE_DIR}/player.c"
    ${PLAT_SOURCES}
)

set(BOMBER_OUTPUT bomber)
set(BOMBER_APP "${CMAKE_CURRENT_BINARY_DIR}/${BOMBER_OUTPUT}.${OUT_SUFFIX}")
file(GLOB BOMBER_HEADERS "${CORE_DIR}/*.h" "${COMMON_DIR}/*.h" "${PLAT_DIR}/*.h")

if(APP_SUFFIX STREQUAL OUT_SUFFIX)
    add_custom_command(
        OUTPUT ${BOMBER_APP}
        COMMAND ${ZCC_EXECUTABLE} ${COMMON_FLAGS} -o ${BOMBER_OUTPUT} ${BOMBER_SOURCES}
        DEPENDS ${BOMBER_SOURCES} ${BOMBER_HEADERS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
else()
    add_custom_command(
        OUTPUT ${BOMBER_APP}
        COMMAND ${ZCC_EXECUTABLE} ${COMMON_FLAGS} -o ${BOMBER_OUTPUT} ${BOMBER_SOURCES}
        COMMAND ${CMAKE_COMMAND} -E rename ${BOMBER_OUTPUT}.${APP_SUFFIX} ${BOMBER_APP}
        DEPENDS ${BOMBER_SOURCES} ${BOMBER_HEADERS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
endif()

add_custom_target(build_${BOMBER_OUTPUT} ALL DEPENDS ${BOMBER_APP})
