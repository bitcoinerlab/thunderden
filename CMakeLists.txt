cmake_minimum_required(VERSION 3.22)
project(ThunderDen VERSION 2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CORE_SOURCE_DIR "" CACHE PATH "Verified Bitcoin Core source")
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

add_library(thunderden STATIC src/keys.cpp src/policy.cpp src/transaction.cpp)
target_include_directories(thunderden PUBLIC src "${CORE_SOURCE_DIR}/src"
  "${CORE_SOURCE_DIR}/src/secp256k1/include" "${CMAKE_BINARY_DIR}/core/src"
  PRIVATE "${CMAKE_BINARY_DIR}")
target_link_libraries(thunderden PUBLIC core_interface bitcoin_common OpenSSL::Crypto)
target_compile_options(thunderden PRIVATE -Wall -Wextra -Werror)
target_link_options(thunderden INTERFACE -Wl,--gc-sections)

enable_testing()
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
  "${CMAKE_SOURCE_DIR}/tests/native_runtime.py" "$<TARGET_FILE:transaction-tests>")
