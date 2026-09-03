# cmake/embed_text.cmake
#
# 把任意文本/二进制文件转成一个 C++17 的 inline constexpr char 数组头文件。
# 通过 cmake -DSRC=... -DDST=... -DVAR=... -P embed_text.cmake 调用，
# 通常被 add_custom_command 包起来，由 ui_core_embed_text() 高层函数生成。
#
# 输出形如：
#   #pragma once
#   inline constexpr char k_app_uix[] = {
#       (char)0x3c, (char)0x21, ...,
#       (char)0x00      // C 字符串结束符，方便直接传给吃 const char* 的 API
#   };
#   inline constexpr unsigned k_app_uix_size = sizeof(k_app_uix) - 1;

if(NOT SRC OR NOT DST OR NOT VAR)
    message(FATAL_ERROR
        "Required: -DSRC=<input> -DDST=<output> -DVAR=<symbol_name>")
endif()
if(NOT EXISTS "${SRC}")
    message(FATAL_ERROR "embed_text: source not found: ${SRC}")
endif()

file(READ "${SRC}" hex HEX)
string(LENGTH "${hex}" hexLen)
math(EXPR byteCount "${hexLen} / 2")

# 头部
set(out "#pragma once\n")
set(out "${out}// AUTO-GENERATED — do not edit\n")
set(out "${out}// source: ${SRC}\n")
set(out "${out}// bytes:  ${byteCount} (excluding terminator)\n\n")
set(out "${out}inline constexpr char ${VAR}[] = {\n    ")

# 主体：每 12 字节换一行，提高可读性
set(i 0)
set(col 0)
while(i LESS hexLen)
    string(SUBSTRING "${hex}" ${i} 2 b)
    set(out "${out}(char)0x${b},")
    math(EXPR i "${i} + 2")
    math(EXPR col "${col} + 1")
    if(col EQUAL 12)
        set(out "${out}\n    ")
        set(col 0)
    else()
        set(out "${out} ")
    endif()
endwhile()

# 结尾终结符 + size 常量
set(out "${out}(char)0x00\n};\n")
set(out "${out}inline constexpr unsigned ${VAR}_size = sizeof(${VAR}) - 1;\n")

file(WRITE "${DST}" "${out}")
