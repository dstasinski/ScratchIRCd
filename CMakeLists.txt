cmake_minimum_required(VERSION 3.16)

project(ScratchIRCd
    VERSION 0.4.0
    DESCRIPTION "Single-server IRC daemon written from scratch in C"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)

enable_testing()

add_executable(scratchircd
    src/main.c
    src/server.c
    src/client.c
    src/channel.c
    src/channel_policy.c
    src/hash.c
    src/irc.c
    src/runtime_config.c
    src/dns.c
    src/modes.c
    src/visibility.c

    src/commands/common.c
    src/commands/dispatch.c
    src/commands/nick.c
    src/commands/user.c
    src/commands/ping.c
    src/commands/joinpart.c
    src/commands/invite.c
    src/commands/kick.c
    src/commands/list.c
    src/commands/mode.c
    src/commands/names.c
    src/commands/notice.c
    src/commands/privmsg.c
    src/commands/topic.c
    src/commands/who.c
    src/commands/whois.c
    src/commands/quit.c
)

target_include_directories(scratchircd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(scratchircd PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(scratchircd PRIVATE Threads::Threads)

add_executable(test_modes tests/test_modes.c src/modes.c)
target_include_directories(test_modes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME modes COMMAND test_modes)

add_executable(test_channel_policy
    tests/test_channel_policy.c
    src/channel.c
    src/channel_policy.c
)
target_include_directories(test_channel_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME channel_policy COMMAND test_channel_policy)

add_executable(test_visibility
    tests/test_visibility.c
    src/channel.c
    src/modes.c
    src/visibility.c
)
target_include_directories(test_visibility PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME visibility COMMAND test_visibility)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(scratchircd PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_modes PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_channel_policy PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_visibility PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS scratchircd RUNTIME DESTINATION bin)
