# Host build of the vendored Wine vbscript engine + the vbs_check driver.
# Same Wine source set and defines as make/CMakeLists_common_standalone.txt.
cmake_minimum_required(VERSION 3.20)
project(vbs_check C CXX)

set(VPX_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(WINE ${VPX_ROOT}/standalone/inc/wine)

# Use the parser the engine build generated (bison writes it into the source
# tree, gitignored), so the harness compiles exactly what the device compiled.
# Only generate it when it is missing or older than the grammar.
set(PARSER_Y ${WINE}/dlls/vbscript/parser.y)
set(PARSER_C ${WINE}/dlls/vbscript/parser.tab.c)
if(NOT EXISTS ${PARSER_C} OR ${PARSER_Y} IS_NEWER_THAN ${PARSER_C})
   find_package(BISON 3.7.5 REQUIRED)
   bison_target(vbsparser ${PARSER_Y} ${PARSER_C} DEFINES_FILE ${WINE}/dlls/vbscript/parser.tab.h)
   message(STATUS "vbs_check: regenerating parser.tab.c with bison ${BISON_VERSION}")
endif()

add_executable(vbs_check
   vbs_check.c
   host_hooks.c
   table_host.c

   ${WINE}/dlls/atl/atl.c
   ${WINE}/dlls/oleaut32/oleaut.c
   ${WINE}/dlls/oleaut32/variant.c
   ${WINE}/dlls/oleaut32/varformat.c
   ${WINE}/dlls/oleaut32/vartype.c
   ${WINE}/dlls/oleaut32/safearray.c
   ${WINE}/dlls/vbscript/compile.c
   ${WINE}/dlls/vbscript/global.c
   ${WINE}/dlls/vbscript/interp.c
   ${WINE}/dlls/vbscript/lex.c
   ${WINE}/dlls/vbscript/regexp.c
   ${WINE}/dlls/vbscript/utils.c
   ${WINE}/dlls/vbscript/vbdisp.c
   ${WINE}/dlls/vbscript/vbregexp.c
   ${WINE}/dlls/vbscript/vbscript.c
   ${WINE}/dlls/vbscript/vbscript_main.c
   ${PARSER_C}
   ${WINE}/dlls/scrrun/dictionary.c
   ${WINE}/dlls/scrrun/filesystem.c
   ${WINE}/dlls/scrrun/scrrun.c
   ${WINE}/dlls/kernelbase/locale.c
   ${WINE}/wine.c
)

set_target_properties(vbs_check PROPERTIES C_STANDARD 99)

target_include_directories(vbs_check PRIVATE
   ${CMAKE_CURRENT_SOURCE_DIR}/shim
   ${VPX_ROOT}
   ${WINE}/include
   ${WINE}/dlls/atl
   ${WINE}/dlls/vbscript
   ${WINE}/dlls/oleaut32
   ${WINE}/dlls/kernelbase
)

target_compile_definitions(vbs_check PRIVATE
   __STANDALONE__
   __WINESRC__
   WINE_UNICODE_NATIVE
   WINE_NO_TRACE_MSGS
   WINE_NO_DEBUG_MSGS
   "__forceinline=__attribute__((always_inline)) inline"
)

target_compile_options(vbs_check PRIVATE -Wno-format -w)
# PuPlayer.ReadScreenType answers with the plugin's own code (plugins/pup/PUPScreenType.h),
# built apart from the Wine sources so their include directories stay out of the C++ headers.
add_library(pup_screentype STATIC pup_screentype.cpp)
set_target_properties(pup_screentype PROPERTIES CXX_STANDARD 20)
target_include_directories(pup_screentype PRIVATE ${VPX_ROOT}/plugins/pup)

target_link_libraries(vbs_check PRIVATE pup_screentype m)
