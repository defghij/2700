cmake_minimum_required(VERSION 3.17)

set(CMAKE_CXX_COMPILER "clang")

project("TwentySevenHundred")

# Names of Labs
set(LAB_ONE "shared_mutable_access")


#### Lab One Configuration
##################################################
add_executable(${LAB_ONE}
                  shared_mutable_access.c
)

target_include_directories(${LAB_ONE}
  PUBLIC $PROJECT_SOURCE_DIR)

target_link_libraries(${LAB_ONE} PUBLIC "m")

message(STATUS "Details-----------------------------")
message(STATUS "Project:...... ${CMAKE_PROJECT_NAME}")
message(STATUS "CXX Compiler:. ${CMAKE_CXX_COMPILER}")
message(STATUS "Source Dir:... ${PROJECT_SOURCE_DIR}")
message(STATUS "Binary Name:.. ${LAB_ONE}")
message(STATUS "Binary Dir:... ${PROJECT_BINARY_DIR}")
message(STATUS "------------------------------------")
##################################################
####


