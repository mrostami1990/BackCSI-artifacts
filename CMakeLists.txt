cmake_minimum_required(VERSION 3.16)
project(backcsi_cpp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(backcsi STATIC src/backcsi.cpp)
target_include_directories(backcsi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
add_executable(backcsi_cli src/main.cpp)
target_link_libraries(backcsi_cli PRIVATE backcsi)
install(TARGETS backcsi backcsi_cli EXPORT backcsiTargets)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT backcsiTargets DESTINATION lib/cmake/backcsi NAMESPACE backcsi::)
