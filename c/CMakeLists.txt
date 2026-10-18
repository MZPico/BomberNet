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
if(PLATFORM STREQUAL "zx128")
    set(PLAT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/platform/zx")
endif()
set(TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tools")

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
                     -pragma-define:CLIB_EXIT_STACK_SIZE=0 -pragma-define:CRT_ENABLE_STDIO=0
                     -pragma-define:CLIB_OPEN_MAX=0 -pragma-redirect:fputc_cons=zx_no_console)
    set(PLAT_SOURCES "${PLAT_DIR}/plat_zx.c" "${PLAT_DIR}/tables.c" "${PLAT_DIR}/tcp_spectranet.c"
                     "${COMMON_DIR}/netdev_soft.c" "${COMMON_DIR}/ws.c" "${COMMON_DIR}/z80_loops.c")
    set(APP_SUFFIX tap)
    set(OUT_SUFFIX tap)
elseif(PLATFORM STREQUAL "zx128")
    # ZX Spectrum 128K: the game as on the 48K, the network driver in RAM bank
    # 6 at C000h (platform/zx/bank6.c, a program of its own), called through
    # tcp_bank6.c, which is linked first so that it stays below C000h.
    set(ZX_FLAGS +zx -lndos -pragma-define:CLIB_EXIT_STACK_SIZE=0 -pragma-define:CRT_ENABLE_STDIO=0
                 -pragma-define:CLIB_OPEN_MAX=0 -pragma-redirect:fputc_cons=zx_no_console)
    set(TARGET_FLAGS ${ZX_FLAGS} -zorg=24000 -pragma-define:REGISTER_SP=65535 -I${CMAKE_CURRENT_BINARY_DIR})
    # the driver in bank 6: esp01 (eLeMeNt / MB03+, tcp_esp01.c, which includes
    # tcp_esp01_at.c) or spectranet (as on the 48K, for comparison and tests)
    set(ZX128_NET "esp01" CACHE STRING "128K network driver: esp01 or spectranet")
    if(ZX128_NET STREQUAL "esp01")
        set(BANK6_SOURCES "${PLAT_DIR}/bank6.c" "${PLAT_DIR}/tcp_esp01.c")
        set(BANK6_DEPENDS "${PLAT_DIR}/tcp_esp01_at.c")
    elseif(ZX128_NET STREQUAL "spectranet")
        set(BANK6_SOURCES "${PLAT_DIR}/bank6.c" "${PLAT_DIR}/tcp_spectranet.c")
    else()
        message(FATAL_ERROR "unknown ZX128_NET ${ZX128_NET}")
    endif()
    set(PLAT_FIRST "${PLAT_DIR}/tcp_bank6.c")
    set(PLAT_SOURCES "${PLAT_DIR}/plat_zx.c" "${PLAT_DIR}/tables.c"
                     "${COMMON_DIR}/netdev_soft.c" "${COMMON_DIR}/ws.c" "${COMMON_DIR}/z80_loops.c")
    set(OUT_SUFFIX tap)
elseif(PLATFORM STREQUAL "cpc")
    # Amstrad CPC 464/6128: an AMSDOS binary; the game takes the machine over
    # in plat_init (own interrupt handler, firmware not called again).
    # Code and data are loaded from 0400h and must end below A67Bh (AMSDOS's
    # top of memory with a disc drive). The variables (BSS) are not in the
    # file: they live in the firmware's RAM from A700h, zeroed by the game
    # once it has taken the machine over (plat_init).
    set(CPC_ORG 0x0400)
    set(TARGET_FLAGS +cpc -create-app -zorg=${CPC_ORG} -pragma-define:__crt_org_bss=0xA700 -pragma-define:CRT_INITIALIZE_BSS=0
                     -pragma-define:CRT_DISABLELOADER=1 -pragma-define:CRT_DISABLE_FIRMWARE_ISR=1
                     -pragma-define:CRT_ENABLE_STDIO=0 -pragma-define:CLIB_OPEN_MAX=0
                     -pragma-define:CLIB_EXIT_STACK_SIZE=0 -pragma-redirect:fputc_cons=cpc_no_console)
    set(PLAT_SOURCES "${PLAT_DIR}/plat_cpc.c" "${PLAT_DIR}/tables.c" "${PLAT_DIR}/tcp_m4.c"
                     "${COMMON_DIR}/netdev_soft.c" "${COMMON_DIR}/ws.c" "${COMMON_DIR}/z80_loops.c")
    set(APP_SUFFIX cpc)
    set(OUT_SUFFIX cpc)
else()
    message(FATAL_ERROR "unknown PLATFORM ${PLATFORM}")
endif()

set(COMMON_FLAGS ${TARGET_FLAGS} --list -m -I${CORE_DIR} -I${COMMON_DIR} -I${PLAT_DIR})

# Link order is memory order. On the Spectrum the first 8 KB land in contended
# RAM (slower while the picture is drawn): menus and lobby go there, the frame
# loop's code and the platform's screen routine last.
set(BOMBER_SOURCES
    ${PLAT_FIRST}
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

if(PLATFORM STREQUAL "zx128")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(
        OUTPUT bank6 bank6.map bank6_entries.h
        COMMAND ${ZCC_EXECUTABLE} ${ZX_FLAGS} -zorg=49152 --list -m -I${CORE_DIR} -I${COMMON_DIR} -I${PLAT_DIR}
                -o bank6 ${BANK6_SOURCES}
        COMMAND ${Python3_EXECUTABLE} ${TOOLS_DIR}/zx128_entries.py bank6.map bank6_entries.h
        DEPENDS ${BANK6_SOURCES} ${BANK6_DEPENDS} ${TOOLS_DIR}/zx128_entries.py "${COMMON_DIR}/tcp.h"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_custom_command(
        OUTPUT ${BOMBER_APP}
        COMMAND ${ZCC_EXECUTABLE} ${COMMON_FLAGS} -o ${BOMBER_OUTPUT} ${BOMBER_SOURCES}
        COMMAND ${Python3_EXECUTABLE} ${TOOLS_DIR}/make_zx128_tap.py ${BOMBER_OUTPUT} ${BOMBER_OUTPUT}.map bank6 ${BOMBER_APP}
        DEPENDS ${BOMBER_SOURCES} ${BOMBER_HEADERS} bank6 bank6_entries.h ${TOOLS_DIR}/make_zx128_tap.py
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
elseif(APP_SUFFIX STREQUAL OUT_SUFFIX)
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

if(PLATFORM STREQUAL "cpc")
    # a disc image (CPC 6128, and the M4 mounts it) besides the AMSDOS file
    get_filename_component(ZCC_DIR ${ZCC_EXECUTABLE} DIRECTORY)
    find_program(APPMAKE_EXECUTABLE NAMES z88dk.z88dk-appmake z88dk-appmake appmake HINTS ${ZCC_DIR})
    set(BOMBER_DSK "${CMAKE_CURRENT_BINARY_DIR}/${BOMBER_OUTPUT}.dsk")
    add_custom_command(
        OUTPUT ${BOMBER_DSK}
        COMMAND ${APPMAKE_EXECUTABLE} +cpc --disk -b ${BOMBER_OUTPUT} --org ${CPC_ORG} -o ${BOMBER_OUTPUT}
        DEPENDS ${BOMBER_APP}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    list(APPEND BOMBER_APP ${BOMBER_DSK})
endif()

add_custom_target(build_${BOMBER_OUTPUT} ALL DEPENDS ${BOMBER_APP})
