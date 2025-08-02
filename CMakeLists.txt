cmake_minimum_required(VERSION 3.10...3.11)
project(activity-check-bot)
set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES "")
set(CMAKE_C_IMPLICIT_LINK_LIBRARIES "")
set(CMAKE_CXX_STANDARD 23)
#set(TGBM_ENABLE_GEN ON)
file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.3/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
        EXPECTED_HASH SHA256=cc155ce02e7945e7b8967ddfaff0b050e958a723ef7aad3766d368940cb15494
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

CPMAddPackage(
        NAME TGBM
        GIT_REPOSITORY https://github.com/PaRat07/TGBM
        GIT_TAG        origin/master
)

CPMAddPackage("gh:PaRat07/SQLiteCpp#origin/master")

add_executable(activity-check-bot
        main.cpp
        src/database.cpp)

target_include_directories(activity-check-bot PUBLIC include)

target_link_libraries(activity-check-bot
        tgbmlib
        SQLiteCpp
        sqlite3
        pthread
        dl
)