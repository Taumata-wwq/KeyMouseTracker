# cmake/UiCoreHelpers.cmake
#
# 下游用户的便利函数。include() 这个文件即可使用。
#
# 用法：
#   include(${UI_CORE_RELEASE_DIR}/cmake/UiCoreHelpers.cmake)
#   ui_core_embed_text(my_app
#       FILE app.uix
#       OUT  app_uix.embed.h
#       VAR  k_app_uix)
#   target_link_libraries(my_app PRIVATE core-ui)
#
# 然后在 main.cpp：
#   #include "app_uix.embed.h"    // 由 cmake 自动生成到 build dir
#   UiPage page = ui_page_load_string(k_app_uix);
#
# 每次源文件改了 ninja/make 会自动重新生成。

set(UI_CORE_HELPERS_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")

# 静态链 core-ui 时下游必须带上的全部 Win32 系统 lib。绝大多数下游应该走
# 下面的 ui_core_link_static() helper 自动接入；这个变量留给手工配置场景。
set(UI_CORE_STATIC_SYSTEM_LIBS
    user32 gdi32 shell32 ole32
    d2d1 d3d11 dxgi dxguid dwrite dwmapi uxtheme
    windowscodecs gdiplus shlwapi
    CACHE STRING "System libs required when linking core-ui statically")

# 一行接入 lib/static/core-ui.lib：自动加链接、系统库、UI_CORE_STATIC 宏。
# 不带这个宏的话 ui_core.h 把 UI_API 展开成 __declspec(dllimport)，下游 link
# 静态归档时会报一堆 unresolved __imp_ui_xxx — 99% 的"不能静态编译"反馈都
# 是这个。
#
# 用法：
#   include(<release>/cmake/UiCoreHelpers.cmake)
#   add_executable(my_app ...)
#   ui_core_link_static(my_app <release>/lib/static/core-ui.lib)
#
# 静态链需要 /MT CRT（避免运行时多份），caller 需要自己 set MSVC_RUNTIME_LIBRARY
# 或在 add_compile_options 加 /MT，这里不强加避免影响下游已有配置。
function(ui_core_link_static TARGET STATIC_LIB_PATH)
    if(NOT EXISTS "${STATIC_LIB_PATH}")
        message(FATAL_ERROR
            "ui_core_link_static: '${STATIC_LIB_PATH}' not found.\n"
            "Pass the absolute path to lib/static/core-ui.lib from the release "
            "package, e.g.:\n"
            "  ui_core_link_static(my_app \"\${CORE_UI_DIR}/lib/static/core-ui.lib\")")
    endif()
    target_link_libraries(${TARGET} PRIVATE
        "${STATIC_LIB_PATH}"
        ${UI_CORE_STATIC_SYSTEM_LIBS}
    )
    target_compile_definitions(${TARGET} PRIVATE UI_CORE_STATIC)
endfunction()

function(ui_core_embed_text TARGET)
    cmake_parse_arguments(EMBED "" "FILE;OUT;VAR" "" ${ARGN})
    if(NOT EMBED_FILE OR NOT EMBED_OUT OR NOT EMBED_VAR)
        message(FATAL_ERROR
            "Usage: ui_core_embed_text(<target> FILE <path> OUT <header> VAR <name>)")
    endif()

    if(IS_ABSOLUTE "${EMBED_FILE}")
        set(_src "${EMBED_FILE}")
    else()
        set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${EMBED_FILE}")
    endif()
    set(_dst "${CMAKE_CURRENT_BINARY_DIR}/${EMBED_OUT}")

    add_custom_command(
        OUTPUT  "${_dst}"
        COMMAND ${CMAKE_COMMAND}
                -DSRC=${_src}
                -DDST=${_dst}
                -DVAR=${EMBED_VAR}
                -P "${UI_CORE_HELPERS_DIR}/embed_text.cmake"
        DEPENDS "${_src}" "${UI_CORE_HELPERS_DIR}/embed_text.cmake"
        COMMENT "Embedding ${EMBED_FILE} → ${EMBED_OUT}"
        VERBATIM
    )
    target_sources(${TARGET} PRIVATE "${_dst}")
    target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

# 二进制版本：把 PNG / JPG / 任意 blob 烤进 unsigned char 数组。配合
# ui_asset_register_blob() 可以做到单文件分发：
#
#   ui_core_embed_binary(my_app
#       FILE assets/logo.png
#       OUT  logo_png.embed.h
#       VAR  k_logo_png)
#
# main.cpp:
#   #include "logo_png.embed.h"
#   ui_init();
#   ui_asset_register_blob("logo.png", k_logo_png, k_logo_png_size);
#   /* 之后 HTML 里 <img src="logo.png"> 会从内存里解析 */
function(ui_core_embed_binary TARGET)
    cmake_parse_arguments(EMBED "" "FILE;OUT;VAR" "" ${ARGN})
    if(NOT EMBED_FILE OR NOT EMBED_OUT OR NOT EMBED_VAR)
        message(FATAL_ERROR
            "Usage: ui_core_embed_binary(<target> FILE <path> OUT <header> VAR <name>)")
    endif()

    if(IS_ABSOLUTE "${EMBED_FILE}")
        set(_src "${EMBED_FILE}")
    else()
        set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${EMBED_FILE}")
    endif()
    set(_dst "${CMAKE_CURRENT_BINARY_DIR}/${EMBED_OUT}")

    add_custom_command(
        OUTPUT  "${_dst}"
        COMMAND ${CMAKE_COMMAND}
                -DSRC=${_src}
                -DDST=${_dst}
                -DVAR=${EMBED_VAR}
                -P "${UI_CORE_HELPERS_DIR}/embed_binary.cmake"
        DEPENDS "${_src}" "${UI_CORE_HELPERS_DIR}/embed_binary.cmake"
        COMMENT "Embedding (binary) ${EMBED_FILE} → ${EMBED_OUT}"
        VERBATIM
    )
    target_sources(${TARGET} PRIVATE "${_dst}")
    target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()
