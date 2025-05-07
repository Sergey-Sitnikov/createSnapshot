cmake_minimum_required(VERSION 3.10)

project(SnapshotApp)

# Установите необходимые флаги для CMake
set(CMAKE_CXX_STANDARD 17)

# Укажите пути до заголовков и статической библиотеки cURL
set(CURL_INCLUDE_DIRS "/home/ssv/curl-7.84.0/include")
set(CURL_LIBRARIES "/home/ssv/curl-7.84.0/lib/.libs/libcurl.a")  # Используйте статическую библиотеку

# Установите флаг для использования статических библиотек OpenSSL
set(OPENSSL_USE_STATIC_LIBS TRUE)

# Установите путь к установленным библиотекам, если необходимо
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} /usr/local)

# Найдите необходимые библиотеки
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nghttp2 REQUIRED)
#find_package(Brotli REQUIRED)
#find_package(Zstd REQUIRED)


# Вывод информации для проверки
message(STATUS "CURL_LIBRARIES: ${CURL_LIBRARIES}")
message(STATUS "CURL_INCLUDE_DIRS: ${CURL_INCLUDE_DIRS}")

# Ваша логика сборки
add_executable(SnapshotApp main.cpp)  # Укажите ваши исходные файлы

# Проверка наличия переменной CURL_INCLUDE_DIRS и добавление ее в путь
if(CURL_INCLUDE_DIRS)
    include_directories(${CURL_INCLUDE_DIRS})
else()
    message(FATAL_ERROR "CURL_INCLUDE_DIRS not set. Please check your cURL installation.")
endif()

# Линковка с библиотеками
target_link_libraries(SnapshotApp PRIVATE 
    ${CURL_LIBRARIES} 
    OpenSSL::SSL 
    OpenSSL::Crypto 
    nghttp2::nghttp2 
#    Brotli::Brotli 
#    Zstd::Zstd
)
