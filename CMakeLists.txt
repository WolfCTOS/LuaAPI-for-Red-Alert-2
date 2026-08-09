cmake_minimum_required(VERSION 3.16)
project(LuaAPI LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Lua static library (exclude standalone interpreters)
file(GLOB LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/*.c)
list(REMOVE_ITEM LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/lua.c)
list(REMOVE_ITEM LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/luac.c)
list(REMOVE_ITEM LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/onelua.c)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua)

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
target_link_libraries(LuaAPI PRIVATE lua)
