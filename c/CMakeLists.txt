cmake_minimum_required(VERSION 3.10)
project(bomber C)

# Same toolchain and flags as MZPico-800-Manager (z88dk sccz80, Sharp MZ target).
find_program(ZCC_EXECUTABLE z88dk.zcc)
if(NOT ZCC_EXECUTABLE)
    message(FATAL_ERROR "z88dk.zcc not found (snap install z88dk)")
endif()

set(COMMON_FLAGS
    +mz
    -lm
    -create-app
    -pragma-define:REGISTER_SP=0xd000
    --list
    -m
)

set(BOMBER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/console.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mzio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/video.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/data.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/game.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/input.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/uc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/net.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/map.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/enemy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/bomb.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/player.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/bomber.c"
)

set(BOMBER_OUTPUT bomber)
set(BOMBER_MZF "${CMAKE_CURRENT_BINARY_DIR}/${BOMBER_OUTPUT}.mzf")

add_custom_command(
    OUTPUT ${BOMBER_MZF}
    COMMAND ${ZCC_EXECUTABLE} ${COMMON_FLAGS} -o ${BOMBER_OUTPUT} ${BOMBER_SOURCES}
    COMMAND ${CMAKE_COMMAND} -E rename ${BOMBER_OUTPUT}.mzt ${BOMBER_MZF}
    DEPENDS ${BOMBER_SOURCES}
            "${CMAKE_CURRENT_SOURCE_DIR}/game.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/data.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/console.h"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

add_custom_target(build_${BOMBER_OUTPUT} ALL DEPENDS ${BOMBER_MZF})
