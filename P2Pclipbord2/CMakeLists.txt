cmake_minimum_required(VERSION 3.14)
project(P2PClipboard)

# Specify where to find dnssd files
set(DNSSD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

# Source files (include app.rc to embed the manifest)
add_executable(P2PClipboard 
    src/main.cpp
    src/NetworkManager.cpp
    src/ClipboardManager.cpp
    src/BLEManager.cpp
    src/BLEManager.h
    src/BLEPayload.h
    app.rc
 "src/BLEPayload.h" "src/ByteUtils.h" "src/ByteUtils.cpp" "src/MessageProtocol.cpp")

# Include directories
target_include_directories(P2PClipboard PRIVATE
    ${DNSSD_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src 
)

# Set C++ standard
set_property(TARGET P2PClipboard PROPERTY CXX_STANDARD 17)
set_property(TARGET P2PClipboard PROPERTY CXX_STANDARD_REQUIRED ON)

# Link against dnssd and Ws2_32
target_link_libraries(P2PClipboard PRIVATE
    ${DNSSD_DIR}/lib/dnssd.lib
    Ws2_32.lib
)

# Copy dnssd.dll to output directory
add_custom_command(TARGET P2PClipboard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${DNSSD_DIR}/bin/dnssd.dll"
    $<TARGET_FILE_DIR:P2PClipboard>
)
