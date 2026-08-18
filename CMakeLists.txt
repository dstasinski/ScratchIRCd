cmake_minimum_required(VERSION 3.16)

project(ScratchIRCd
    VERSION 0.5.0
    DESCRIPTION "Single-server IRC daemon written from scratch in C"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(SQLite3 REQUIRED)

find_path(ARGON2_INCLUDE_DIR argon2.h)
find_library(ARGON2_LIBRARY NAMES argon2)
if(NOT ARGON2_INCLUDE_DIR OR NOT ARGON2_LIBRARY)
    message(FATAL_ERROR "libargon2 development files are required (Debian/Ubuntu: libargon2-dev)")
endif()

enable_testing()

add_executable(scratchircd
    src/main.c src/server.c src/client.c src/channel.c src/channel_policy.c
    src/hash.c src/irc.c src/runtime_config.c src/dns.c src/modes.c src/oper.c
    src/operator_db.c src/visibility.c

    src/commands/admin.c src/commands/away.c src/commands/common.c
    src/commands/dispatch.c src/commands/invite.c src/commands/ison.c
    src/commands/joinpart.c src/commands/kick.c src/commands/list.c
    src/commands/lusers.c src/commands/mode.c src/commands/motd.c
    src/commands/names.c src/commands/nick.c src/commands/notice.c
    src/commands/oper.c src/commands/operator_admin.c src/commands/pass.c
    src/commands/ping.c src/commands/privmsg.c src/commands/quit.c
    src/commands/rules.c src/commands/topic.c src/commands/user.c
    src/commands/userhost.c src/commands/userip.c src/commands/who.c
    src/commands/whois.c
)

target_include_directories(scratchircd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ARGON2_INCLUDE_DIR}
)
target_compile_definitions(scratchircd PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(scratchircd PRIVATE Threads::Threads ${ARGON2_LIBRARY} SQLite::SQLite3)

add_executable(scratchircd-mkpasswd tools/mkpasswd.c)
target_include_directories(scratchircd-mkpasswd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ARGON2_INCLUDE_DIR}
)
target_link_libraries(scratchircd-mkpasswd PRIVATE ${ARGON2_LIBRARY})

add_executable(test_modes tests/test_modes.c src/modes.c)
target_include_directories(test_modes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME modes COMMAND test_modes)

add_executable(test_channel_policy tests/test_channel_policy.c src/channel.c src/channel_policy.c)
target_include_directories(test_channel_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME channel_policy COMMAND test_channel_policy)

add_executable(test_visibility tests/test_visibility.c src/channel.c src/channel_policy.c src/modes.c src/visibility.c)
target_include_directories(test_visibility PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME visibility COMMAND test_visibility)

add_executable(test_oper tests/test_oper.c src/oper.c)
target_include_directories(test_oper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME oper_permissions COMMAND test_oper)

add_executable(test_operator_db tests/test_operator_db.c src/operator_db.c)
target_include_directories(test_operator_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_operator_db PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(test_operator_db PRIVATE SQLite::SQLite3)
add_test(NAME operator_database COMMAND test_operator_db)

add_test(
    NAME protocol_integration
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_protocol.py
            $<TARGET_FILE:scratchircd>
            $<TARGET_FILE:scratchircd-mkpasswd>
)
set_tests_properties(protocol_integration PROPERTIES TIMEOUT 45)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target scratchircd scratchircd-mkpasswd test_modes test_channel_policy test_visibility test_oper test_operator_db)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

install(TARGETS scratchircd scratchircd-mkpasswd RUNTIME DESTINATION bin)
