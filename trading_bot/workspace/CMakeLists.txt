cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(cpp_bot)

# Set the path to OpenSSL 3.0 installation
set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl@3.0")

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Ensure OpenSSL was found
if (OPENSSL_FOUND)
    message(STATUS "OpenSSL found.")
    message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
    include_directories(${OPENSSL_INCLUDE_DIR})

    # List your source files, including the main .cpp file and others
    set(SOURCE_FILES
        qpl_bot_strategy_equities.cpp
        jsonUtils.cpp
        httpUtils.cpp
        ntpUtils.cpp
        socketUtils.cpp
        wsUtils.cpp
        tradingBot.cpp
        modelUtils.cpp
        # Add other .cpp files if needed
    )

    # Create the executable
    add_executable(cpp_bot_exe ${SOURCE_FILES})

    # Link OpenSSL libraries
    target_link_libraries(cpp_bot_exe ${OPENSSL_LIBRARIES})

else()

    message(FATAL_ERROR "OpenSSL not found. Please install OpenSSL 3.0.")
    
endif()
