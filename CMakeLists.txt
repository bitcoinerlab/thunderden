cmake_minimum_required(VERSION 3.22)
project(ThunderDen LANGUAGES C CXX)
option(TD_BUILD_TESTS "Build development tests and probes" ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CORE_SOURCE_DIR "" CACHE PATH "Verified Bitcoin Core source")
set(UR_SOURCE_DIR "" CACHE PATH "Verified bc-ur source")
set(BIP39_WORDLIST "" CACHE FILEPATH "Pinned BIP39 English wordlist")
set(BIP39_WORDLIST_SHA256 "" CACHE STRING "Expected wordlist SHA256")

file(SHA256 "${BIP39_WORDLIST}" wordlist_hash)
if(NOT wordlist_hash STREQUAL BIP39_WORDLIST_SHA256)
  message(FATAL_ERROR "BIP39 wordlist hash mismatch")
endif()
file(STRINGS "${BIP39_WORDLIST}" words)
list(LENGTH words word_count)
if(NOT word_count EQUAL 2048)
  message(FATAL_ERROR "Expected 2048 BIP39 words")
endif()
set(word_array "#pragma once\n#include <array>\n#include <string_view>\nnamespace td { inline constexpr std::array<std::string_view, 2048> ENGLISH{\n")
foreach(word IN LISTS words)
  string(APPEND word_array "\"${word}\",\n")
endforeach()
string(APPEND word_array "}; }\n")
file(WRITE "${CMAKE_BINARY_DIR}/english.h" "${word_array}")

find_package(OpenSSL REQUIRED COMPONENTS Crypto)
foreach(option BUILD_BITCOIN_BIN BUILD_DAEMON BUILD_GUI BUILD_CLI BUILD_TESTS
    BUILD_TX BUILD_UTIL BUILD_KERNEL_LIB BUILD_UTIL_CHAINSTATE BUILD_BENCH
    BUILD_FUZZ_BINARY ENABLE_WALLET ENABLE_IPC ENABLE_EXTERNAL_SIGNER
    WITH_EMBEDDED_ASMAP WITH_ZMQ WITH_USDT WITH_CCACHE INSTALL_MAN)
  set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(REDUCE_EXPORTS ON CACHE BOOL "" FORCE)
set(APPEND_CXXFLAGS "-ffunction-sections -fdata-sections" CACHE STRING "" FORCE)
set(APPEND_CFLAGS "-ffunction-sections -fdata-sections" CACHE STRING "" FORCE)
add_subdirectory("${CORE_SOURCE_DIR}" core EXCLUDE_FROM_ALL)

file(GLOB ur_sources CONFIGURE_DEPENDS "${UR_SOURCE_DIR}/src/*.cpp" "${UR_SOURCE_DIR}/src/*.c")
add_library(bc-ur STATIC ${ur_sources})
target_include_directories(bc-ur SYSTEM PUBLIC "${UR_SOURCE_DIR}/src")
target_compile_options(bc-ur PRIVATE -ffunction-sections -fdata-sections)
# The pinned sources rely on transitive standard-library includes.
target_compile_options(bc-ur PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstdint -include cstring>")

find_package(PkgConfig REQUIRED)
pkg_check_modules(QR REQUIRED IMPORTED_TARGET libqrencode zbar libv4l2)

add_library(thunderden STATIC src/keys.cpp src/policy.cpp src/transaction.cpp src/transport.cpp src/qr.cpp
  src/review.cpp src/terminal.cpp src/application.cpp src/qr_commands.cpp src/hardware.cpp src/camera.cpp src/scan.cpp src/isolation.cpp)
target_include_directories(thunderden PUBLIC src "${CORE_SOURCE_DIR}/src"
  "${CORE_SOURCE_DIR}/src/secp256k1/include" "${CMAKE_BINARY_DIR}/core/src"
  PRIVATE "${CMAKE_BINARY_DIR}")
target_link_libraries(thunderden PUBLIC core_interface bitcoin_common univalue OpenSSL::Crypto bc-ur PkgConfig::QR)
target_compile_options(thunderden PRIVATE -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
target_link_options(thunderden INTERFACE -Wl,--gc-sections)

add_executable(thunderden-signer src/main.cpp)
target_link_libraries(thunderden-signer PRIVATE thunderden)
target_compile_options(thunderden-signer PRIVATE -Wall -Wextra -Werror)
install(TARGETS thunderden-signer RUNTIME DESTINATION bin)

add_executable(thunderden-scanner src/scanner_main.cpp)
target_link_libraries(thunderden-scanner PRIVATE thunderden)
target_compile_options(thunderden-scanner PRIVATE -Wall -Wextra -Werror)
install(TARGETS thunderden-scanner RUNTIME DESTINATION bin)

if(TD_BUILD_TESTS)
enable_testing()
add_executable(qr-command-runner tests/qr_command_runner.cpp)
target_link_libraries(qr-command-runner PRIVATE thunderden)
target_compile_options(qr-command-runner PRIVATE -Wall -Wextra -Werror)
add_test(NAME qr-commands COMMAND qr-command-runner --test)
add_executable(thunderden-tests tests/foundation.cpp)
target_link_libraries(thunderden-tests PRIVATE thunderden)
target_compile_options(thunderden-tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME foundation COMMAND thunderden-tests)

add_executable(transaction-tests tests/transactions.cpp)
target_link_libraries(transaction-tests PRIVATE thunderden)
target_compile_options(transaction-tests PRIVATE -Wall -Wextra -Werror)
target_link_options(transaction-tests PRIVATE -Wl,--wrap=secp256k1_ecdsa_sign
  -Wl,--wrap=secp256k1_schnorrsig_sign32)
add_test(NAME transactions COMMAND transaction-tests)

foreach(program core-key-probe core-key-edges)
  string(REPLACE "-" "_" source "${program}")
  add_executable(${program} "tests/${source}.cpp")
  target_include_directories(${program} PRIVATE "${CORE_SOURCE_DIR}/src"
    "${CORE_SOURCE_DIR}/src/secp256k1/include" "${CMAKE_BINARY_DIR}/core/src")
  target_link_libraries(${program} PRIVATE core_interface bitcoin_common)
  target_compile_options(${program} PRIVATE -Wall -Wextra -Werror)
  target_link_options(${program} PRIVATE -Wl,--gc-sections)
endforeach()
target_link_options(core-key-edges PRIVATE -Wl,--wrap=secp256k1_ec_seckey_tweak_add)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME core-keys COMMAND Python3::Interpreter -B "${CMAKE_SOURCE_DIR}/tests/core_keys.py")
add_test(NAME native-runtime COMMAND Python3::Interpreter -B
  "${CMAKE_SOURCE_DIR}/tests/native_runtime.py" "$<TARGET_FILE:transaction-tests>" "$<TARGET_FILE:thunderden-signer>")

add_executable(transport-tests tests/transport.cpp)
target_link_libraries(transport-tests PRIVATE thunderden)
target_compile_options(transport-tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME transport COMMAND transport-tests)

add_executable(application-tests tests/application.cpp)
target_link_libraries(application-tests PRIVATE thunderden)
target_compile_options(application-tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME application COMMAND application-tests)
add_test(NAME export-compat COMMAND Python3::Interpreter -B "${CMAKE_SOURCE_DIR}/tests/export_compat.py"
  "$<TARGET_FILE:application-tests>")

add_executable(terminal-probe tests/terminal_probe.cpp)
target_link_libraries(terminal-probe PRIVATE thunderden)
target_compile_options(terminal-probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME terminal COMMAND Python3::Interpreter -B "${CMAKE_SOURCE_DIR}/tests/terminal.py"
  "$<TARGET_FILE:terminal-probe>" "$<TARGET_FILE:thunderden-signer>")

add_executable(qr-image-probe tests/qr_image_probe.cpp)
target_link_libraries(qr-image-probe PRIVATE thunderden)
target_compile_options(qr-image-probe PRIVATE -Wall -Wextra -Werror)

add_executable(camera-tests tests/camera.cpp)
target_link_libraries(camera-tests PRIVATE thunderden)
target_compile_options(camera-tests PRIVATE -Wall -Wextra -Werror)
target_link_options(camera-tests PRIVATE -Wl,--wrap=v4l2_open -Wl,--wrap=v4l2_ioctl
  -Wl,--wrap=v4l2_read -Wl,--wrap=v4l2_close -Wl,--wrap=poll
  -Wl,--wrap=open -Wl,--wrap=ioctl -Wl,--wrap=mmap -Wl,--wrap=munmap -Wl,--wrap=close)
add_test(NAME camera COMMAND camera-tests)

add_executable(isolation-tests tests/isolation.cpp)
pkg_check_modules(CAMERA_TEST REQUIRED IMPORTED_TARGET libv4lconvert libjpeg)
target_link_libraries(isolation-tests PRIVATE thunderden PkgConfig::CAMERA_TEST)
target_compile_options(isolation-tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME isolation COMMAND isolation-tests)
set_tests_properties(isolation PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
endif()
