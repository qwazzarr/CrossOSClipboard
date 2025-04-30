cmake_minimum_required(VERSION 3.14)
project(P2PClipboard)

# Use the static MSVC CRT
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Where to find dnssd
set(DNSSD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

# -----------------------------------------------------------------------------
# 1) Core library target
# -----------------------------------------------------------------------------
add_library(P2PClipboardLib STATIC
    src/NetworkManager.cpp
    src/ClipboardManager.cpp
    src/BLEManager.cpp
    src/UUIDGenerator.cpp
    src/ClipboardEncryption.cpp
    src/MessageProtocol.cpp
    src/ByteUtils.cpp
    src/ClipboardImageHandler.cpp
)

target_include_directories(P2PClipboardLib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DNSSD_DIR}/include
)

target_link_libraries(P2PClipboardLib PUBLIC
    "${DNSSD_DIR}/lib/dnssd.lib"
    Ws2_32.lib
)

set_property(TARGET P2PClipboardLib PROPERTY CXX_STANDARD 17)
set_property(TARGET P2PClipboardLib PROPERTY CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# 2) Main application
# -----------------------------------------------------------------------------
add_executable(P2PClipboard
    src/main.cpp
    app.rc
)

target_link_libraries(P2PClipboard PRIVATE
    P2PClipboardLib
)

# Copy dnssd.dll alongside the executable
add_custom_command(TARGET P2PClipboard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DNSSD_DIR}/bin/dnssd.dll"
        $<TARGET_FILE_DIR:P2PClipboard>
)

set_property(TARGET P2PClipboard PROPERTY CXX_STANDARD 17)
set_property(TARGET P2PClipboard PROPERTY CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# 3) FetchContent → Catch2 for tests
# -----------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.4.0
)

FetchContent_MakeAvailable(Catch2)

# -----------------------------------------------------------------------------
# 4) Tests
# -----------------------------------------------------------------------------
enable_testing()

add_executable(ClipboardTests
    tests/test_byteutils.cpp
    tests/test_uuidgenerator.cpp
    tests/test_clipboardencryption.cpp
    tests/test_messageprotocol.cpp
    tests/test_clipboardmanager.cpp
)

target_link_libraries(ClipboardTests PRIVATE
    Catch2::Catch2WithMain
    P2PClipboardLib
)

target_include_directories(ClipboardTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_property(TARGET ClipboardTests PROPERTY CXX_STANDARD 17)
set_property(TARGET ClipboardTests PROPERTY CXX_STANDARD_REQUIRED ON)

# Register with CTest
add_test(NAME ClipboardTests COMMAND ClipboardTests)
