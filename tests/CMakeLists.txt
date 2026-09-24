set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

function(add_bot_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/Helpers" "${CMAKE_SOURCE_DIR}/Playback")
    # The bot gets /utf-8 from Qt; dpp's unicode_emoji.h needs it.
    target_compile_options(${name} PRIVATE /utf-8 /we4715)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_bot_test(PcmBufferTest PcmBufferTest.cpp Check.h ../Playback/PcmBuffer.cpp)
add_bot_test(FailureStreakTest FailureStreakTest.cpp Check.h ../Playback/FailureStreak.cpp)
add_bot_test(VoiceDrainWatchdogTest VoiceDrainWatchdogTest.cpp Check.h ../Playback/VoiceDrainWatchdog.cpp)
add_bot_test(TimeTextTest TimeTextTest.cpp ../Helpers/TimeText.cpp)
add_bot_test(PlaybackButtonsTest PlaybackButtonsTest.cpp Check.h ../Helpers/PlaybackButtons.cpp)
add_bot_test(InteractionsTest InteractionsTest.cpp Check.h ../Helpers/Interactions.cpp ../Helpers/PlaybackButtons.cpp)

target_link_libraries(PlaybackButtonsTest PRIVATE dpp)
target_link_libraries(InteractionsTest PRIVATE dpp Qt${QT_VERSION_MAJOR}::Core)

# dpp.dll needs the OpenSSL, opus and zlib DLLs shipped beside it, and Qt's bin
# directory is on PATH only inside Qt Creator.
set(DPP_PATH "PATH=path_list_prepend:$<TARGET_FILE_DIR:dpp>")
set(QT_PATH "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>")
set_tests_properties(PlaybackButtonsTest PROPERTIES ENVIRONMENT_MODIFICATION "${DPP_PATH}")
set_tests_properties(InteractionsTest PROPERTIES ENVIRONMENT_MODIFICATION "${DPP_PATH};${QT_PATH}")
