cmake_minimum_required(VERSION 3.14)
project(ScreenshotCapture)

set(CMAKE_CXX_STANDARD 17)

find_package(Qt5 REQUIRED COMPONENTS Widgets)
find_package(CURL REQUIRED)
find_package(Zstd REQUIRED)
find_package(Brotli REQUIRED)
# find_package(nghttp2 REQUIRED) # Закомментируйте это

set(CMAKE_AUTOMOC ON)

add_executable(ScreenshotCapture main.cpp)

# Альтернативный подход к связыванию для Zstd и Brotli
find_library(ZSTD_LIBRARY zstd
    HINTS /usr/lib/x86_64-linux-gnu) # Измените путь, если необходимо

find_library(BROTLI_LIBRARY brotlidec
    HINTS /usr/lib/x86_64-linux-gnu) # Измените путь, если необходимо

find_library(NGHTTP2_LIBRARY nghttp2
    HINTS /usr/lib/x86_64-linux-gnu) # Добавьте это

if (ZSTD_LIBRARY AND BROTLI_LIBRARY AND NGHTTP2_LIBRARY)
    message(STATUS "Found Zstd library: ${ZSTD_LIBRARY}")
    message(STATUS "Found Brotli library: ${BROTLI_LIBRARY}")
    message(STATUS "Found nghttp2 library: ${NGHTTP2_LIBRARY}")
    target_link_libraries(ScreenshotCapture Qt5::Widgets CURL::libcurl ${ZSTD_LIBRARY} ${BROTLI_LIBRARY} ${NGHTTP2_LIBRARY}) # Обновите это
else()
    if (NOT ZSTD_LIBRARY)
        message(FATAL_ERROR "Zstd library not found")
    endif()
    if (NOT BROTLI_LIBRARY)
        message(FATAL_ERROR "Brotli library not found")
    endif()
    if (NOT NGHTTP2_LIBRARY)
        message(FATAL_ERROR "nghttp2 library not found")
    endif()
endif()
