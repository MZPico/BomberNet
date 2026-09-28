cmake_minimum_required(VERSION 3.10)
project(bomber C)

# Same toolchain and flags as MZPico-800-Manager (z88dk sccz80, Sharp MZ target).
find_program(ZCC_EXECUTABLE z88dk.zcc)
if(NOT ZCC_EXECUTABLE)
    message(FATAL_ERROR "z88dk.zcc not found (snap install z88dk)")
endif()

# Platform to build (platform/<name>); the core is the same for all.
set(PLATFORM "mz" CACHE STRING "target platform")
set(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/core")
set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")
set(PLAT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/platform/${PLATFORM}")

set(COMMON_FLAGS
    +mz
    -lm
    -create-app
    -pragma-define:REGISTER_SP=0xd000
    --list
    -m
    -I${CORE_DIR}
    -I${COMMON_DIR}
    -I${PLAT_DIR}
)

set(BOMBER_SOURCES
    "${PLAT_DIR}/plat_mz.c"
    "${PLAT_DIR}/uc_mz.c"
    "${PLAT_DIR}/tables.c"
    "${COMMON_DIR}/netdev_card.c"
    "${CORE_DIR}/video.c"
    "${CORE_DIR}/data.c"
    "${CORE_DIR}/game.c"
    "${CORE_DIR}/input.c"
    "${CORE_DIR}/netplay.c"
    "${CORE_DIR}/map.c"
    "${CORE_DIR}/enemy.c"
    "${CORE_DIR}/bomb.c"
    "${CORE_DIR}/player.c"
    "${CORE_DIR}/bomber.c"
)

set(BOMBER_OUTPUT bomber)
set(BOMBER_MZF "${CMAKE_CURRENT_BINARY_DIR}/${BOMBER_OUTPUT}.mzf")

add_custom_command(
    OUTPUT ${BOMBER_MZF}
    COMMAND ${ZCC_EXECUTABLE} ${COMMON_FLAGS} -o ${BOMBER_OUTPUT} ${BOMBER_SOURCES}
    COMMAND ${CMAKE_COMMAND} -E rename ${BOMBER_OUTPUT}.mzt ${BOMBER_MZF}
    DEPENDS ${BOMBER_SOURCES}
            "${CORE_DIR}/game.h" "${CORE_DIR}/platform.h" "${CORE_DIR}/netdev.h" "${CORE_DIR}/data.h"
            "${COMMON_DIR}/uc.h" "${PLAT_DIR}/plat_config.h" "${PLAT_DIR}/tables.h"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

add_custom_target(build_${BOMBER_OUTPUT} ALL DEPENDS ${BOMBER_MZF})
