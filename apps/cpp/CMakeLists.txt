cmake_minimum_required(VERSION 3.16)
project(cpp_api CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# Crow (vendored, header-only) uses standalone Asio when CROW_USE_BOOST is undefined.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQXX REQUIRED libpqxx)
pkg_check_modules(HIREDIS REQUIRED hiredis)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

include_directories(src src/vendor)

set(LIB_SOURCES
    src/handlers.cpp
    src/db.cpp
    src/cache.cpp
    src/auth.cpp
    src/access_log.cpp
)

add_executable(server src/main.cpp ${LIB_SOURCES})
target_include_directories(server PRIVATE ${LIBPQXX_INCLUDE_DIRS} ${HIREDIS_INCLUDE_DIRS})
target_link_libraries(server PRIVATE ${LIBPQXX_LIBRARIES} ${HIREDIS_LIBRARIES}
    hiredis_ssl OpenSSL::SSL OpenSSL::Crypto crypt Threads::Threads)
target_link_directories(server PRIVATE ${LIBPQXX_LIBRARY_DIRS} ${HIREDIS_LIBRARY_DIRS})

# Tests
enable_testing()
find_package(GTest REQUIRED)
add_executable(unit_tests tests/test_handlers.cpp ${LIB_SOURCES})
target_include_directories(unit_tests PRIVATE ${LIBPQXX_INCLUDE_DIRS} ${HIREDIS_INCLUDE_DIRS})
target_link_libraries(unit_tests PRIVATE GTest::gtest GTest::gtest_main
    ${LIBPQXX_LIBRARIES} ${HIREDIS_LIBRARIES} hiredis_ssl
    OpenSSL::SSL OpenSSL::Crypto crypt Threads::Threads)
target_link_directories(unit_tests PRIVATE ${LIBPQXX_LIBRARY_DIRS} ${HIREDIS_LIBRARY_DIRS})
add_test(NAME unit_tests COMMAND unit_tests)
