# cmake/embed_binary.cmake
#
# 把任意二进制文件烤成一个 C++17 inline constexpr unsigned char 数组头文件。
# 跟 embed_text.cmake 的区别：
#   - char    数组：text 用，末尾补 0x00 当 C 字符串终结符
#   - uchar   数组：binary 用，**无终结符**（PNG/JPG 这种内含 0x00 字节）
#   - 类型用  unsigned char，不是 char，避免下游做 sign-extension
#
# 通过 cmake -DSRC=... -DDST=... -DVAR=... -P embed_binary.cmake 调用，
# 通常被 add_custom_command 包起来，由 ui_core_embed_binary() 高层函数生成。
#
# 输出形如：
#   #pragma once
#   inline constexpr unsigned char k_logo_png[] = {
#       0x89, 0x50, 0x4E, 0x47, ..., 0x82
#   };
#   inline constexpr unsigned k_logo_png_size = sizeof(k_logo_png);

if(NOT SRC OR NOT DST OR NOT VAR)
    message(FATAL_ERROR
        "Required: -DSRC=<input> -DDST=<output> -DVAR=<symbol_name>")
endif()
if(NOT EXISTS "${SRC}")
    message(FATAL_ERROR "embed_binary: source not found: ${SRC}")
endif()

file(READ "${SRC}" hex HEX)
string(LENGTH "${hex}" hexLen)
math(EXPR byteCount "${hexLen} / 2")

set(out "#pragma once\n")
set(out "${out}// AUTO-GENERATED — do not edit\n")
set(out "${out}// source: ${SRC}\n")
set(out "${out}// bytes:  ${byteCount}\n\n")
set(out "${out}inline constexpr unsigned char ${VAR}[] = {\n    ")

set(i 0)
set(col 0)
while(i LESS hexLen)
    string(SUBSTRING "${hex}" ${i} 2 b)
    set(out "${out}0x${b}")
    math(EXPR i "${i} + 2")
    math(EXPR col "${col} + 1")
    if(i LESS hexLen)
        set(out "${out},")
        if(col EQUAL 12)
            set(out "${out}\n    ")
            set(col 0)
        else()
            set(out "${out} ")
        endif()
    endif()
endwhile()

set(out "${out}\n};\n")
set(out "${out}inline constexpr unsigned ${VAR}_size = sizeof(${VAR});\n")

file(WRITE "${DST}" "${out}")
