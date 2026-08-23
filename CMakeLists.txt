cmake_minimum_required(VERSION 3.16)
project(LuaAPI LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statically link the CRT (/MT, /MTd for Debug) so the binaries do not
# depend on vcruntime140.dll / msvcp140.dll inside the game process.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Prevent windows.h min/max macros from breaking YRpp/std headers.
# FMT_CONSTEVAL= works around C7595 (bundled fmt v9 vs MSVC 19.4x+ consteval strictness).
add_compile_definitions(NOMINMAX FMT_CONSTEVAL=)

if (MSVC)
    add_compile_options(/utf-8)
endif()

# Lua static library (exclude standalone interpreters)
file(GLOB LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/*.c)
list(REMOVE_ITEM LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/lua.c)
list(REMOVE_ITEM LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/luac.c)
list(REMOVE_ITEM LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/onelua.c)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua)

# MinHook (x86 trampoline hooking) - no longer linked, retained for reference
file(GLOB MINHOOK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/src/hde/*.c
)
add_library(minhook STATIC ${MINHOOK_SOURCES})
target_include_directories(minhook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/include)

# LuaAPI DLL
file(GLOB LUAAPI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(LuaAPI SHARED ${LUAAPI_SOURCES})
target_include_directories(LuaAPI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sol2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/YRpp
)
target_link_libraries(LuaAPI PRIVATE lua minhook)

# Copy scripts next to the DLL so the runtime can find scripts/init.lua
add_custom_command(TARGET LuaAPI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts
        $<TARGET_FILE_DIR:LuaAPI>/scripts
    COMMENT "Copying Lua scripts to output directory"
)

# Injector executable (native Win32 GUI, no console window)
add_executable(injector WIN32 src/injector_gui.cpp)
target_compile_definitions(injector PRIVATE UNICODE _UNICODE)
target_link_libraries(injector PRIVATE user32 gdi32 shell32)

# Auto-deploy binaries to the game directory (repo root) after a successful build
foreach(_target LuaAPI injector)
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${_target}>
            ${CMAKE_SOURCE_DIR}
        COMMENT "Deploying ${_target} to game directory"
    )
endforeach()
