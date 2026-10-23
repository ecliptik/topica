# Topica: a Markdown writer for blogs, for the classic Macintosh.
# Built with Retro68: scripts/build.sh --preset lite|full.
cmake_minimum_required(VERSION 3.13)
project(Topica VERSION 0.7.0 LANGUAGES C)

# Lite: the Macintosh 512Ke (512K, no MultiFinder). Full: everything else.
option(TOPICA_LITE "Build for 512K Macs: fewer windows, a smaller undo history" OFF)
option(TOPICA_DEBUG "Log to a Topica Log file beside the application" OFF)
option(TOPICA_TALKIP "Publish over LocalTalk with talkip (TALKIP_DIR) instead of MacTCP" OFF)

if(NOT DEFINED TOPICA_VERSION_DISPLAY)
    set(TOPICA_VERSION_DISPLAY "${PROJECT_VERSION}")
endif()

if(TOPICA_LITE)
    set(TOPICA_MAX_DOCS 2)
    set(TOPICA_UNDO_BYTES 8192)
    set(TOPICA_SIZE_PREFERRED 320)
    set(TOPICA_SIZE_MINIMUM 256)
else()
    set(TOPICA_MAX_DOCS 6)
    set(TOPICA_UNDO_BYTES 32768)
    set(TOPICA_SIZE_PREFERRED 768)
    set(TOPICA_SIZE_MINIMUM 448)
endif()

# 'vers': the major version, then minor and bug-fix as one BCD byte
set(TOPICA_VERS_MAJOR ${PROJECT_VERSION_MAJOR})
math(EXPR TOPICA_VERS_MINOR "${PROJECT_VERSION_MINOR} * 16 + ${PROJECT_VERSION_PATCH}")

configure_file(resources/topica.r.in ${CMAKE_CURRENT_BINARY_DIR}/topica.r @ONLY)
configure_file(resources/icons.r ${CMAKE_CURRENT_BINARY_DIR}/icons.r COPYONLY)
configure_file(resources/balloons.r ${CMAKE_CURRENT_BINARY_DIR}/balloons.r COPYONLY)
list(APPEND REZ_INCLUDE_PATH ${CMAKE_CURRENT_BINARY_DIR})

set(TOPICA_SOURCES
    app/main.c
    app/docwin.c
    app/te_editor.c
    app/fileio.c
    app/postinfo.c
    app/util.c
    app/ae.c
    app/findwin.c
    app/settings.c
    app/headings.c
    app/page.c
    app/preview.c
    app/print.c
    app/spellwin.c
    app/publishwin.c
    app/theme.c
    core/textconv.c
    core/frontmatter.c
    core/undo.c
    core/post.c
    core/find.c
    core/prefs.c
    core/recovery.c
    core/mdscan.c
    core/mdpage.c
    core/layout.c
    core/mdhtml.c
    core/vi.c
    core/dict.c
    core/spell.c
    core/publish.c
)

# Publish's TCP: MacTCP, or talkip's stack in the application (a 512K Mac
# on LocalTalk, which has no room for MacTCP). talkip is a separate
# repository: https://github.com/ecliptik/talkip, next to this one.
# Drag and drop (System 7.5) and Word Services (System 7): not in Lite,
# for 512K Macs on System 6
if(NOT TOPICA_LITE)
    list(APPEND TOPICA_SOURCES app/textdrag.c app/wordsvc.c)
endif()

if(TOPICA_TALKIP)
    if(NOT DEFINED TALKIP_DIR)
        set(TALKIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../talkip)
    endif()
    if(NOT EXISTS ${TALKIP_DIR}/CMakeLists.txt)
        message(FATAL_ERROR "TOPICA_TALKIP needs talkip: clone it next to Topica, or pass -DTALKIP_DIR=<path>")
    endif()
    add_subdirectory(${TALKIP_DIR} talkip)
    list(APPEND TOPICA_SOURCES app/net_talkip.c)
else()
    list(APPEND TOPICA_SOURCES app/net_mactcp.c)
endif()

add_application(Topica CREATOR "TPCA" ${TOPICA_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/topica.r)

if(TOPICA_TALKIP)
    target_link_libraries(Topica talkip)
endif()

target_compile_definitions(Topica PRIVATE
    TOPICA_MAX_DOCS=${TOPICA_MAX_DOCS}
    TOPICA_UNDO_BYTES=${TOPICA_UNDO_BYTES}L
    $<$<BOOL:${TOPICA_LITE}>:TOPICA_LITE>
    $<$<BOOL:${TOPICA_DEBUG}>:TOPICA_DEBUG>
    $<$<BOOL:${TOPICA_TALKIP}>:TOPICA_TALKIP>
)

# The 68000 (a Mac Plus, a 512Ke), smallest code, no dead code.
target_compile_options(Topica PRIVATE -m68000 -Os -Wall -Wextra -Wno-unused-parameter
    -ffunction-sections -fdata-sections)
set_target_properties(Topica PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
