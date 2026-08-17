cmake_minimum_required(VERSION 3.16)

project(simple_ircd
    VERSION 0.3.0
    DESCRIPTION "Small modular IRC server foundation written in C"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(simple-ircd
    src/main.c
    src/server.c
    src/client.c
    src/channel.c
    src/hash.c
    src/irc.c

    # IRC command subsystem.  Command implementations live only here.
    src/commands/common.c
    src/commands/dispatch.c
    src/commands/nick.c
    src/commands/user.c
    src/commands/ping.c
    src/commands/joinpart.c
    src/commands/privmsg.c
    src/commands/quit.c
)

target_include_directories(simple-ircd
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Expose POSIX networking APIs such as getaddrinfo() under strict C11 builds.
target_compile_definitions(simple-ircd PRIVATE _POSIX_C_SOURCE=200809L)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(simple-ircd PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS simple-ircd RUNTIME DESTINATION bin)
