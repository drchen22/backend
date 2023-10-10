cmake_minimum_required(VERSION 3.10)

project(backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

find_library(PQXX pqxx)
find_library(PQ pq)
find_package(nlohmann_json 3.2.0 REQUIRED)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE headers CONFIGURE_DEPENDS include/*.h include/*.hpp)
add_executable(${PROJECT_NAME} ${sources} ${headers})
target_include_directories(${PROJECT_NAME} PUBLIC include)


target_link_libraries(${PROJECT_NAME} PUBLIC pqxx pq)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)