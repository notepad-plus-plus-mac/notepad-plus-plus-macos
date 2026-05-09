cmake_minimum_required(VERSION 3.20)
project(NotepadPlusPlusLinux C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

set(SCI_ROOT "${CMAKE_SOURCE_DIR}/../scintilla")
set(LEX_ROOT "${CMAKE_SOURCE_DIR}/../lexilla")

# -----------------------------------------------------------------------------
# Scintilla — core + GTK3 backend
# -----------------------------------------------------------------------------
file(GLOB SCI_CORE_SRCS "${SCI_ROOT}/src/*.cxx")

set(SCI_GTK_SRCS
    "${SCI_ROOT}/gtk/PlatGTK.cxx"
    "${SCI_ROOT}/gtk/ScintillaGTK.cxx"
    "${SCI_ROOT}/gtk/ScintillaGTKAccessible.cxx"
    "${SCI_ROOT}/gtk/scintilla-marshal.c"
)

add_library(scintilla STATIC ${SCI_CORE_SRCS} ${SCI_GTK_SRCS})

target_include_directories(scintilla PUBLIC
    "${SCI_ROOT}/include"
    "${SCI_ROOT}/src"
    "${SCI_ROOT}/gtk"
    ${GTK3_INCLUDE_DIRS}
)

target_compile_definitions(scintilla PUBLIC GTK SCI_NAMESPACE)
target_compile_options(scintilla PRIVATE ${GTK3_CFLAGS_OTHER})
target_link_libraries(scintilla PUBLIC ${GTK3_LIBRARIES})

# -----------------------------------------------------------------------------
# Lexilla — syntax highlighting (platform-agnostic)
# -----------------------------------------------------------------------------
file(GLOB LEX_SRCS
    "${LEX_ROOT}/src/*.cxx"
    "${LEX_ROOT}/lexers/*.cxx"
    "${LEX_ROOT}/lexlib/*.cxx"
)
# LexUser.cxx is now included (patched for Linux — windows.h removed, _itoa→sprintf)

add_library(lexilla STATIC ${LEX_SRCS})

target_include_directories(lexilla PUBLIC
    "${LEX_ROOT}/include"
    "${LEX_ROOT}/src"
    "${LEX_ROOT}/lexlib"
    "${LEX_ROOT}/access"
    "${SCI_ROOT}/include"
    "${SCI_ROOT}/src"
)

# GCC 14 no longer pulls intptr_t in via <stddef.h>; Sci_Position.h needs it.
target_compile_options(lexilla PRIVATE "-include" "cstdint")

# -----------------------------------------------------------------------------
# Application — GTK3 + Scintilla (C + C++ wrapper for Lexilla)
# -----------------------------------------------------------------------------
add_executable(notepad++
    src/main.c
    src/editor.c
    src/encoding.c
    src/prefs.c
    src/shortcutmap.c
    src/statusbar.c
    src/findreplace.c
    src/findinfiles.c
    src/columneditor.c
    src/autocomplete.c
    src/udl.c
    src/gitgutter.c
    src/macro.c
    src/toolbar.c
    src/stylestore.c
    src/styleeditor.c
    src/lexer.c
    src/i18n.c
    src/session.c
    src/backup.c
    src/doclist.c
    src/workspace.c
    src/funclist.c
    src/lexilla_bridge.cpp
)

target_include_directories(notepad++ PRIVATE
    "${SCI_ROOT}/include"
    "${SCI_ROOT}/src"
    "${SCI_ROOT}/gtk"
    "${LEX_ROOT}/include"
    ${GTK3_INCLUDE_DIRS}
)

target_compile_definitions(notepad++ PRIVATE
    GTK
    RESOURCES_DIR="${CMAKE_SOURCE_DIR}/../resources"
)
target_compile_options(notepad++ PRIVATE ${GTK3_CFLAGS_OTHER})
# lexilla_bridge.cpp needs cstdint for the same reason as lexilla
set_source_files_properties(src/lexilla_bridge.cpp PROPERTIES
    COMPILE_OPTIONS "-include;cstdint")

target_link_libraries(notepad++ PRIVATE
    scintilla
    lexilla
    ${GTK3_LIBRARIES}
    m
)
