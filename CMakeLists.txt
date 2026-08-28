cmake_minimum_required(VERSION 3.16)

project(ScratchIRCd
    VERSION 0.33.0
    DESCRIPTION "Single-server IRC daemon written from scratch in C"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SCRATCHIRCD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(SCRATCHIRCD_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    if(SCRATCHIRCD_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
    if(SCRATCHIRCD_ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
elseif(SCRATCHIRCD_WARNINGS_AS_ERRORS OR SCRATCHIRCD_ENABLE_SANITIZERS)
    message(FATAL_ERROR "Strict warning and sanitizer options require GCC or Clang")
endif()

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
    src/main.c src/server.c src/client.c src/cloak.c src/channel.c src/channel_policy.c src/channel_log.c
    src/hash.c src/irc.c src/ircv3.c src/runtime_config.c src/dns.c src/dnsbl.c src/geoip.c src/geoban_db.c src/history_db.c src/mail.c src/message_policy.c src/modes.c src/nospoof.c src/oper.c src/presence.c src/usermode_policy.c
    src/operator_db.c src/ban_db.c src/nickserv.c src/nickserv_db.c src/chanserv.c src/chanserv_db.c src/chanserv_db_logging.c src/chanserv_persist.c src/memoserv.c src/memoserv_db.c src/memoserv_db_extra.c src/visibility.c

    src/commands/admin.c src/commands/authenticate.c src/commands/away.c src/commands/cap.c src/commands/chanserv.c src/commands/chanserv_admin.c src/commands/chathistory.c src/commands/common.c
    src/commands/deafmute.c src/commands/die.c src/commands/dispatch.c src/commands/geoban.c src/commands/globops.c src/commands/identify.c src/commands/invite.c src/commands/ison.c
    src/commands/joinpart.c src/commands/kick.c src/commands/kill.c src/commands/kline.c src/commands/knock.c
    src/commands/list.c src/commands/lusers.c
    src/commands/memoserv.c src/commands/memoserv_admin.c src/commands/mode.c src/commands/mode_persist.c src/commands/mode_control.c src/commands/moderation_policy.c src/commands/motd.c src/commands/names.c
    src/commands/nick.c src/commands/nickserv.c src/commands/nickserv_admin.c src/commands/notice.c src/commands/oper.c
    src/commands/operator_admin.c src/commands/pass.c src/commands/ping.c
    src/commands/privmsg.c src/commands/quit.c src/commands/rehash.c
    src/commands/restart.c src/commands/rules.c src/commands/sajoin.c
    src/commands/samode.c src/commands/sapart.c src/commands/sethost.c
    src/commands/setident.c src/commands/setname.c src/commands/silence.c src/commands/topic.c
    src/commands/user.c src/commands/userhost.c src/commands/userip.c
    src/commands/wallops.c src/commands/watch.c src/commands/webirc.c src/commands/who.c
    src/commands/whois.c src/commands/whowas.c src/commands/zline.c
)

set_source_files_properties(src/commands/mode.c PROPERTIES COMPILE_DEFINITIONS "command_mode=command_mode_core")
set_source_files_properties(src/commands/mode_persist.c PROPERTIES COMPILE_DEFINITIONS "command_mode=command_mode_policy_core")
set_source_files_properties(src/commands/privmsg.c PROPERTIES COMPILE_DEFINITIONS "command_privmsg=command_privmsg_core")
set_source_files_properties(src/commands/notice.c PROPERTIES COMPILE_DEFINITIONS "command_notice=command_notice_core")

target_include_directories(scratchircd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${ARGON2_INCLUDE_DIR} ${MAXMINDDB_INCLUDE_DIR})
target_compile_definitions(scratchircd PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(scratchircd PRIVATE Threads::Threads ${ARGON2_LIBRARY} ${MAXMINDDB_LIBRARY} SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto)

add_executable(scratchircd-mkpasswd tools/mkpasswd.c)
target_include_directories(scratchircd-mkpasswd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${ARGON2_INCLUDE_DIR})
target_link_libraries(scratchircd-mkpasswd PRIVATE ${ARGON2_LIBRARY})

add_executable(test_client_identity tests/test_client_identity.c src/client.c)
target_include_directories(test_client_identity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${MAXMINDDB_INCLUDE_DIR})
target_link_libraries(test_client_identity PRIVATE OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME client_identity COMMAND test_client_identity)

add_executable(test_client_output tests/test_client_output.c src/client.c)
target_include_directories(test_client_output PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${MAXMINDDB_INCLUDE_DIR})
target_link_libraries(test_client_output PRIVATE OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME client_output_queue COMMAND test_client_output)

add_executable(test_cloak tests/test_cloak.c src/cloak.c src/client.c src/modes.c)
target_include_directories(test_cloak PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${MAXMINDDB_INCLUDE_DIR})
target_link_libraries(test_cloak PRIVATE OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME cloak COMMAND test_cloak)

add_executable(test_geoip tests/test_geoip.c src/geoip.c)
target_include_directories(test_geoip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${MAXMINDDB_INCLUDE_DIR})
target_link_libraries(test_geoip PRIVATE ${MAXMINDDB_LIBRARY})
add_test(NAME geoip COMMAND test_geoip)

add_executable(test_geoban_db tests/test_geoban_db.c src/geoban_db.c)
target_include_directories(test_geoban_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${MAXMINDDB_INCLUDE_DIR})
target_compile_definitions(test_geoban_db PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(test_geoban_db PRIVATE SQLite::SQLite3)
add_test(NAME geoban_database COMMAND test_geoban_db)

add_executable(test_dnsbl tests/test_dnsbl.c src/dnsbl.c)
target_include_directories(test_dnsbl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_dnsbl PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(test_dnsbl PRIVATE Threads::Threads)
add_test(NAME dnsbl COMMAND test_dnsbl)

add_executable(test_runtime_config tests/test_runtime_config.c src/runtime_config.c)
target_include_directories(test_runtime_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_runtime_config PRIVATE _POSIX_C_SOURCE=200809L)
add_test(NAME runtime_config COMMAND test_runtime_config ${CMAKE_CURRENT_SOURCE_DIR}/ircd.conf.example)

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

add_executable(test_history_db tests/test_history_db.c src/history_db.c)
target_include_directories(test_history_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_history_db PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(test_history_db PRIVATE SQLite::SQLite3)
add_test(NAME history_database COMMAND test_history_db)

add_executable(test_nickserv_db tests/test_nickserv_db.c src/nickserv_db.c)
target_include_directories(test_nickserv_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_nickserv_db PRIVATE _POSIX_C_SOURCE=200809L IRCD_NICKSERV_ACCOUNT_HARD_MAX=2U)
target_link_libraries(test_nickserv_db PRIVATE SQLite::SQLite3 OpenSSL::Crypto)
add_test(NAME nickserv_database COMMAND test_nickserv_db)

add_executable(test_chanserv_db tests/test_chanserv_db.c src/chanserv_db.c)
target_include_directories(test_chanserv_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_chanserv_db PRIVATE _POSIX_C_SOURCE=200809L IRCD_CHANSERV_REGISTRATION_HARD_MAX=2U IRCD_CHANSERV_ACCESS_HARD_MAX=2U)
target_link_libraries(test_chanserv_db PRIVATE SQLite::SQLite3)
add_test(NAME chanserv_database COMMAND test_chanserv_db)

add_executable(test_chanserv_persist tests/test_chanserv_persist.c src/chanserv_db.c src/chanserv_persist.c src/channel.c src/channel_policy.c src/client.c)
target_include_directories(test_chanserv_persist PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${MAXMINDDB_INCLUDE_DIR})
target_compile_definitions(test_chanserv_persist PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(test_chanserv_persist PRIVATE SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME chanserv_persistence COMMAND test_chanserv_persist)

add_executable(test_channel_log tests/test_channel_log.c src/channel_log.c src/chanserv_db.c src/chanserv_db_logging.c src/client.c src/modes.c src/oper.c)
target_include_directories(test_channel_log PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${MAXMINDDB_INCLUDE_DIR})
target_compile_definitions(test_channel_log PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(test_channel_log PRIVATE SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME channel_log_rotation COMMAND test_channel_log)

add_executable(test_chanserv_logging_db tests/test_chanserv_logging_db.c src/chanserv_db.c src/chanserv_db_logging.c)
target_include_directories(test_chanserv_logging_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_chanserv_logging_db PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(test_chanserv_logging_db PRIVATE SQLite::SQLite3)
add_test(NAME chanserv_logging_database COMMAND test_chanserv_logging_db)

add_executable(test_memoserv_db tests/test_memoserv_db.c src/memoserv_db.c src/memoserv_db_extra.c)
target_include_directories(test_memoserv_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_memoserv_db PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(test_memoserv_db PRIVATE SQLite::SQLite3)
add_test(NAME memoserv_database COMMAND test_memoserv_db)

add_test(NAME protocol_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_protocol.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(protocol_integration PROPERTIES TIMEOUT 60)
add_test(NAME registration_input_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_registration_input.py $<TARGET_FILE:scratchircd>)
set_tests_properties(registration_input_integration PROPERTIES TIMEOUT 60)
add_test(NAME operator_actions_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_operator_actions.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(operator_actions_integration PROPERTIES TIMEOUT 60)
add_test(NAME stats_envelope_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_stats_envelope.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(stats_envelope_integration PROPERTIES TIMEOUT 60)
add_test(NAME channel_logging_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_channel_logging.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(channel_logging_integration PROPERTIES TIMEOUT 60)
add_test(NAME connection_limit_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_connection_limit.py $<TARGET_FILE:scratchircd>)
set_tests_properties(connection_limit_integration PROPERTIES TIMEOUT 60)
add_test(NAME channel_limits_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_channel_limits.py $<TARGET_FILE:scratchircd>)
set_tests_properties(channel_limits_integration PROPERTIES TIMEOUT 60)
add_test(NAME chanserv_legacy_wire_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_chanserv_wire_legacy.py $<TARGET_FILE:scratchircd>)
set_tests_properties(chanserv_legacy_wire_integration PROPERTIES TIMEOUT 60)
add_test(NAME command_budget_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_command_budget.py $<TARGET_FILE:scratchircd>)
set_tests_properties(command_budget_integration PROPERTIES TIMEOUT 60)
add_test(NAME fanout_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_fanout.py $<TARGET_FILE:scratchircd>)
set_tests_properties(fanout_integration PROPERTIES TIMEOUT 60)
add_test(NAME operator_identity_security_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_operator_identity_security.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(operator_identity_security_integration PROPERTIES TIMEOUT 60)
add_test(NAME registration_timeout_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_registration_timeout.py $<TARGET_FILE:scratchircd>)
set_tests_properties(registration_timeout_integration PROPERTIES TIMEOUT 60)
add_test(NAME rehash_consistency_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_rehash_consistency.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(rehash_consistency_integration PROPERTIES TIMEOUT 60)
add_test(NAME snotice_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_snotice.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(snotice_integration PROPERTIES TIMEOUT 60)
add_test(NAME operator_override_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_operator_override.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(operator_override_integration PROPERTIES TIMEOUT 60)
add_test(NAME tls_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_tls.py $<TARGET_FILE:scratchircd>)
set_tests_properties(tls_integration PROPERTIES TIMEOUT 60)
add_test(NAME webirc_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_webirc.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(webirc_integration PROPERTIES TIMEOUT 60)
add_test(NAME zline_cidr_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_zline_cidr.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(zline_cidr_integration PROPERTIES TIMEOUT 60)
add_test(NAME nickserv_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_nickserv.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(nickserv_integration PROPERTIES TIMEOUT 120)
add_test(NAME sasl_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_sasl.py $<TARGET_FILE:scratchircd>)
set_tests_properties(sasl_integration PROPERTIES TIMEOUT 60)
add_test(NAME ircv3_cap_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_ircv3_cap.py $<TARGET_FILE:scratchircd>)
set_tests_properties(ircv3_cap_integration PROPERTIES TIMEOUT 60)
add_test(NAME history_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_history.py $<TARGET_FILE:scratchircd>)
set_tests_properties(history_integration PROPERTIES TIMEOUT 60)
add_test(NAME chanserv_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_chanserv.py $<TARGET_FILE:scratchircd>)
set_tests_properties(chanserv_integration PROPERTIES TIMEOUT 60)
add_test(NAME chanserv_persistence_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_chanserv_persistence.py $<TARGET_FILE:scratchircd>)
set_tests_properties(chanserv_persistence_integration PROPERTIES TIMEOUT 60)
add_test(NAME memoserv_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_memoserv.py $<TARGET_FILE:scratchircd>)
set_tests_properties(memoserv_integration PROPERTIES TIMEOUT 60)
add_test(NAME presence_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_presence.py $<TARGET_FILE:scratchircd>)
set_tests_properties(presence_integration PROPERTIES TIMEOUT 60)
add_test(NAME user_modes_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_user_modes.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(user_modes_integration PROPERTIES TIMEOUT 60)
add_test(NAME message_policy_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_message_policy.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(message_policy_integration PROPERTIES TIMEOUT 60)
add_test(NAME knock_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_knock.py $<TARGET_FILE:scratchircd>)
set_tests_properties(knock_integration PROPERTIES TIMEOUT 60)
add_test(NAME isupport_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_isupport.py $<TARGET_FILE:scratchircd>)
set_tests_properties(isupport_integration PROPERTIES TIMEOUT 60)
add_test(NAME moderation_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_moderation.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(moderation_integration PROPERTIES TIMEOUT 60)
add_test(NAME geoban_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_geoban.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(geoban_integration PROPERTIES TIMEOUT 60)
add_test(NAME channel_authority_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_channel_authority.py $<TARGET_FILE:scratchircd>)
set_tests_properties(channel_authority_integration PROPERTIES TIMEOUT 60)
add_test(NAME kick_lifecycle_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_kick_lifecycle.py $<TARGET_FILE:scratchircd>)
set_tests_properties(kick_lifecycle_integration PROPERTIES TIMEOUT 60)
add_test(NAME die_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_die.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(die_integration PROPERTIES TIMEOUT 60)
add_test(NAME restart_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_restart.py $<TARGET_FILE:scratchircd> $<TARGET_FILE:scratchircd-mkpasswd>)
set_tests_properties(restart_integration PROPERTIES TIMEOUT 60)
add_test(NAME general_flood_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_general_flood.py $<TARGET_FILE:scratchircd>)
set_tests_properties(general_flood_integration PROPERTIES TIMEOUT 60)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target scratchircd scratchircd-mkpasswd test_client_identity test_client_output test_cloak test_geoip test_geoban_db test_dnsbl test_runtime_config test_modes test_channel_policy test_visibility test_oper test_operator_db test_ban_db test_history_db test_nickserv_db test_chanserv_db test_chanserv_persist test_channel_log test_chanserv_logging_db test_memoserv_db)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

install(TARGETS scratchircd scratchircd-mkpasswd RUNTIME DESTINATION bin)