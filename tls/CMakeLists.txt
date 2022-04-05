cmake_minimum_required(VERSION 3.5)

project(tls_test LANGUAGES CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(Threads::Threads INTERFACE IMPORTED)

find_package(Boost REQUIRED COMPONENTS thread system)
find_package(OpenSSL REQUIRED)

add_executable(tls_test tls_test.cpp)

target_link_libraries(
		${PROJECT_NAME}

		PRIVATE Threads::Threads
		PRIVATE Boost::system
		PRIVATE Boost::thread
		PRIVATE OpenSSL::Crypto
		PRIVATE OpenSSL::SSL
)
