cmake_minimum_required(VERSION 3.14)

project(discord_bot LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
find_package(FFmpeg REQUIRED)

add_library(dpp SHARED IMPORTED)
set_target_properties(dpp PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/../libdpp-10.1.6-win64-debug-vs2022/bin/dpp.dll"
    IMPORTED_IMPLIB   "${CMAKE_SOURCE_DIR}/../libdpp-10.1.6-win64-debug-vs2022/lib/dpp-10.1/dpp.lib"
)

target_include_directories(dpp INTERFACE
    "${CMAKE_SOURCE_DIR}/../libdpp-10.1.6-win64-debug-vs2022/include/dpp-10.1"
)

add_executable(discord_bot
  main.cpp
  # token.json
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
  Commands/StopCommand.h Commands/StopCommand.cpp
  Commands/SkipCommand.h Commands/SkipCommand.cpp
  Commands/QueueCommand.h Commands/QueueCommand.cpp
  Commands/PlayCommand.h Commands/PlayCommand.cpp

  Runner/WindowsProcessRunner.h Runner/WindowsProcessRunner.cpp
  Playback/PlaybackController.h Playback/PlaybackController.cpp
  VoiceConnector.h VoiceConnector.cpp
  Messages.h
  Commands/PauseCommand.h Commands/PauseCommand.cpp
  Commands/ResumeCommand.h Commands/ResumeCommand.cpp
  Commands/LoopCommand.h Commands/LoopCommand.cpp
  Commands/ForwardCommand.h Commands/ForwardCommand.cpp
  Commands/PlaylistCommand.h Commands/PlaylistCommand.cpp
  Playback/PcmResampler.h Playback/PcmResampler.cpp
  Helpers/Labels.h Helpers/Labels.cpp
  Helpers/YoutubeInput.h Helpers/YoutubeInput.cpp
  Playback/ResolverWorker.h Playback/ResolverWorker.cpp
  Playback/SongPlayer.h Playback/SongPlayer.cpp
  Playback/VoiceDrainWatchdog.h Playback/VoiceDrainWatchdog.cpp
  Playback/Song.h
)

target_include_directories(discord_bot PRIVATE "${CMAKE_SOURCE_DIR}")
target_include_directories(discord_bot PRIVATE "${CMAKE_SOURCE_DIR}/Commands")
target_include_directories(discord_bot PRIVATE "${CMAKE_SOURCE_DIR}/Helpers")
target_include_directories(discord_bot PRIVATE "${CMAKE_SOURCE_DIR}/Playback")
target_include_directories(discord_bot PRIVATE "${CMAKE_SOURCE_DIR}/Runner")
target_include_directories(discord_bot PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
    "C:/vcpkg/installed/x64-windows/include"
)

target_link_libraries(discord_bot Qt${QT_VERSION_MAJOR}::Core dpp winmm)

# A function that can fall off its end without returning is a bug, not a warning.
target_compile_options(discord_bot PRIVATE /we4715)
target_link_libraries(discord_bot
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    swresample
)

# Keep the runtime pieces next to the exe: the freshly matching dpp.dll
# (a stale copy once meant silently running an old DPP) and the CA bundle
# OpenSSL needs for certificate verification on Windows.
add_custom_command(TARGET discord_bot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:dpp>"
        "${CMAKE_SOURCE_DIR}/cacert.pem"
        "$<TARGET_FILE_DIR:discord_bot>"
)

include(GNUInstallDirs)
install(TARGETS discord_bot
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
