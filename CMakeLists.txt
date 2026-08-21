cmake_minimum_required(VERSION 3.16)

project(ScratchIRCd
    VERSION 0.11.0
    DESCRIPTION "Single-server IRC daemon written from scratch in C"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

find_path(ARGON2_INCLUDE_DIR argon2.h)
find_library(ARGON2_LIBRARY NAMES argon2)
if(NOT ARGON2_INCLUDE_DIR OR NOT ARGON2_LIBRARY)
    message(FATAL_ERROR "libargon2 development files are required (Debian/Ubuntu: libargon2-dev)")
endif()

find_path(MAXMINDDB_INCLUDE_DIR maxminddb.h)
find_library(MAXMINDDB_LIBRARY NAMES maxminddb)
if(NOT MAXMINDDB_INCLUDE_DIR OR NOT MAXMINDDB_LIBRARY)
    message(FATAL_ERROR "libmaxminddb development files are required (Debian/Ubuntu: libmaxminddb-dev)")
endif()

enable_testing()

add_executable(scratchircd
    src/main.c src/server.c src/client.c src/channel.c src/channel_policy.c
    src/hash.c src/irc.c src/runtime_config.c src/dns.c src/geoip.c src/modes.c src/oper.c
    src/operator_db.c src/ban_db.c src/visibility.c

    src/commands/admin.c src/commands/away.c src/commands/common.c
    src/commands/dispatch.c src/commands/invite.c src/commands/ison.c
    src/commands/joinpart.c src/commands/kick.c src/commands/kill.c
    src/commands/kline.c src/commands/list.c src/commands/lusers.c
    src/commands/mode.c src/commands/motd.c src/commands/names.c
    src/commands/nick.c src/commands/notice.c src/commands/oper.c
    src/commands/operator_admin.c src/commands/pass.c src/commands/ping.c
    src/commands/privmsg.c src/commands/quit.c src/commands/rehash.c
    src/commands/restart.c src/commands/rules.c src/commands/sajoin.c
    src/commands/samode.c src/commands/sapart.c src/commands/sethost.c
    src/commands/setident.c src/commands/setname.c src/commands/topic.c
    src/commands/user.c src/commands/userhost.c src/commands/userip.c
    src/commands/wallops.c src/commands/webirc.c src/commands/who.c
    src/commands/whois.c src/commands/zline.c
)

target_include_directories(scratchircd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ARGON2_INCLUDE_DIR}
    ${MAXMINDDB_INCLUDE_DIR}
)
target_compile_definitions(scratchircd PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(scratchircd PRIVATE Threads::Threads ${ARGON2_LIBRARY} ${MAXMINDDB_LIBRARY} SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto)

add_executable(scratchircd-mkpasswd tools/mkpasswd.c)
target_include_directories(scratchircd-mkpasswd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ARGON2_INCLUDE_DIR}
)
target_link_libraries(scratchircd-mkpasswd PRIVATE ${ARGON2_LIBRARY})

add_executable(test_client_identity tests/test_client_identity.c src/client.c)
target_include_directories(test_client_identity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${MAXMINDDB_INCLUDE_DIR})
target_link_libraries(test_client_identity PRIVATE OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME client_identity COMMAND test_client_identity)

add_executable(test_geoip tests/test_geoip.c src/geoip.c)
target_include_directories(test_geoip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${MAXMINDDB_INCLUDE_DIR})
target_link_libraries(test_geoip PRIVATE ${MAXMINDDB_LIBRARY})
add_test(NAME geoip COMMAND test_geoip)

add_executable(test_modes tests/test_modes.c src/modes.c)
target_include_directories(test_modes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME modes COMMAND test_modes)

add_executable(test_channel_policy tests/test_channel_policy.c src/client.c src/channel.c src/channel_policy.c)
target_include_directories(test_channel_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${MAXMINDDB_INCLUDE_DIR})
target_link_libraries(test_channel_policy PRIVATE OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME channel_policy COMMAND test_channel_policy)

add_executable(test_visibility tests/test_visibility.c src/client.c src/channel.c src/channel_policy.c src/modes.c src/visibility.c)
target_include_directories(test_visibility PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${MAXMINDDB_INCLUDE_DIR})
target_link_libraries(test_visibility PRIVATE OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME visibility COMMAND test_visibility)

add_executable(test_oper tests/test_oper.c src/oper.c)
target_include_directories(test_oper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME oper_permissions COMMAND test_oper)

add_executable(test_operator_db tests/test_operator_db.c src/operator_db.c)
target_include_directories(test_operator_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_operator_db PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(test_operator_db PRIVATE SQLite::SQLite3)
add_test(NAME operator_database COMMAND test_operator_db)

add_executable(test_ban_db tests/test_ban_db.c src/ban_db.c)
target_include_directories(test_ban_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_ban_db PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(test_ban_db PRIVATE SQLite::SQLite3)
add_test(NAME ban_database COMMAND test_ban_db)

add_test(NAME protocol_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_protocol.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(protocol_integration PROPERTIES TIMEOUT 60)
add_test(NAME operator_actions_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_operator_actions.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(operator_actions_integration PROPERTIES TIMEOUT 60)
add_test(NAME operator_override_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_operator_override.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(operator_override_integration PROPERTIES TIMEOUT 60)
add_test(NAME tls_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_tls.py $<TARGET_FILE:scratchircd>)
set_tests_properties(tls_integration PROPERTIES TIMEOUT 60)
add_test(NAME webirc_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_webirc.py $<TARGET_FILE:scratchircd>)
set_tests_properties(webirc_integration PROPERTIES TIMEOUT 60)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target scratchircd scratchircd-mkpasswd test_client_identity test_geoip test_modes test_channel_policy test_visibility test_oper test_operator_db test_ban_db)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

install(TARGETS scratchircd scratchircd-mkpasswd RUNTIME DESTINATION bin)
