cmake_minimum_required(VERSION 3.14)

project(discord_bot LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

add_library(dpp SHARED IMPORTED)
set_target_properties(dpp PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/../libdpp-10.1.4-win64-debug-vs2022/bin/dpp.dll"
    IMPORTED_IMPLIB   "${CMAKE_SOURCE_DIR}/../libdpp-10.1.4-win64-debug-vs2022/lib/dpp-10.1/dpp.lib"
)

target_include_directories(dpp INTERFACE
    "${CMAKE_SOURCE_DIR}/../libdpp-10.1.4-win64-debug-vs2022/include/dpp-10.1"
)

add_executable(discord_bot
  main.cpp
  token.json
  TokenReader.h
  TokenReader.cpp
  CtrlMainServer.h
  CtrlMainServer.cpp
  Commands/ICommand.h
  Commands/ICommand.cpp
  Commands/Command.h
  Commands/Command.cpp
  Commands/JoinCommand.h
  Commands/JoinCommand.cpp
  Commands/CommandHandler.h
  Commands/CommandHandler.cpp
  Commands/LeaveCommand.h Commands/LeaveCommand.cpp
  Commands/StartTimerCommand.h Commands/StartTimerCommand.cpp
)

target_include_directories(discord_bot PRIVATE "${CMAKE_SOURCE_DIR}/Commands")

target_link_libraries(discord_bot Qt${QT_VERSION_MAJOR}::Core dpp)

include(GNUInstallDirs)
install(TARGETS discord_bot
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
