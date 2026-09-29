set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

function(add_bot_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/Helpers" "${CMAKE_SOURCE_DIR}/Playback"
        "${CMAKE_SOURCE_DIR}/Runner" "${CMAKE_SOURCE_DIR}/Resolver")
    # The bot gets /utf-8 from Qt; dpp's unicode_emoji.h needs it.
    target_compile_options(${name} PRIVATE /utf-8 /we4715)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_bot_test(PcmBufferTest PcmBufferTest.cpp Check.h ../Playback/PcmBuffer.cpp)
add_bot_test(FailureStreakTest FailureStreakTest.cpp Check.h ../Playback/FailureStreak.cpp)
add_bot_test(VoiceDrainWatchdogTest VoiceDrainWatchdogTest.cpp Check.h ../Playback/VoiceDrainWatchdog.cpp)
add_bot_test(EmptyRoomClockTest EmptyRoomClockTest.cpp Check.h ../EmptyRoomClock.cpp)
add_bot_test(TimeTextTest TimeTextTest.cpp ../Helpers/TimeText.cpp)
add_bot_test(PlaybackButtonsTest PlaybackButtonsTest.cpp Check.h ../Helpers/PlaybackButtons.cpp)
add_bot_test(InteractionsTest InteractionsTest.cpp Check.h ../Helpers/Interactions.cpp ../Helpers/PlaybackButtons.cpp)
add_bot_test(NowPlayingLineTest NowPlayingLineTest.cpp Check.h ../Commands/NowPlayingLine.cpp ../Helpers/TimeText.cpp)
add_bot_test(QueueTextTest QueueTextTest.cpp Check.h ../Commands/QueueText.cpp)
add_bot_test(WindowsCommandLineTest WindowsCommandLineTest.cpp Check.h ../Runner/WindowsCommandLine.cpp)
add_bot_test(YtDlpResolverTest YtDlpResolverTest.cpp Check.h ../Resolver/YtDlpResolver.cpp ../Helpers/YoutubeInput.cpp)
add_bot_test(QueueEditTest QueueEditTest.cpp Check.h ../Playback/QueueEdit.cpp)
add_bot_test(LogTest LogTest.cpp Check.h ../Helpers/Log.cpp)

target_link_libraries(PlaybackButtonsTest PRIVATE dpp)
target_link_libraries(InteractionsTest PRIVATE dpp Qt${QT_VERSION_MAJOR}::Core)
target_link_libraries(YtDlpResolverTest PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_link_libraries(QueueEditTest PRIVATE dpp)
target_link_libraries(LogTest PRIVATE dpp Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(NowPlayingLineTest PRIVATE "${CMAKE_SOURCE_DIR}/Commands")
target_include_directories(QueueTextTest PRIVATE "${CMAKE_SOURCE_DIR}/Commands")

# dpp.dll needs the OpenSSL, opus and zlib DLLs shipped beside it, and Qt's bin
# directory is on PATH only inside Qt Creator.
set(DPP_PATH "PATH=path_list_prepend:$<TARGET_FILE_DIR:dpp>")
set(QT_PATH "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>")
set_tests_properties(PlaybackButtonsTest PROPERTIES ENVIRONMENT_MODIFICATION "${DPP_PATH}")
set_tests_properties(InteractionsTest PROPERTIES ENVIRONMENT_MODIFICATION "${DPP_PATH};${QT_PATH}")
set_tests_properties(YtDlpResolverTest PROPERTIES ENVIRONMENT_MODIFICATION "${QT_PATH}")
set_tests_properties(QueueEditTest PROPERTIES ENVIRONMENT_MODIFICATION "${DPP_PATH}")
set_tests_properties(LogTest PROPERTIES ENVIRONMENT_MODIFICATION "${DPP_PATH};${QT_PATH}")
