# SPDX-License-Identifier: MIT

function(whisker_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

whisker_test(test_config ${CMAKE_SOURCE_DIR}/src/config.cpp)
whisker_test(test_dictation ${CMAKE_SOURCE_DIR}/src/dictation.cpp)
whisker_test(test_deckpad ${CMAKE_SOURCE_DIR}/src/deckpad.cpp)
whisker_test(test_haptic ${CMAKE_SOURCE_DIR}/src/haptic.cpp ${CMAKE_SOURCE_DIR}/src/deckpad.cpp)
whisker_test(test_gamepad ${CMAKE_SOURCE_DIR}/src/gamepad.cpp)
whisker_test(test_keyboard ${CMAKE_SOURCE_DIR}/src/keyboard.cpp)
target_include_directories(test_keyboard PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
target_link_libraries(test_keyboard PRIVATE ${LIBEVDEV_LIBRARIES})

whisker_test(test_overlay
    ${CMAKE_SOURCE_DIR}/src/overlay_gamescope.cpp
    ${CMAKE_SOURCE_DIR}/src/overlay_layershell.cpp)
target_link_libraries(test_overlay PRIVATE whisker_protocol X11::X11 ${WAYLAND_CLIENT_LIBRARIES})

add_test(NAME service_kill_mode
    COMMAND sh -c "grep -qx KillMode=mixed whisker.service && grep -qx KillMode=mixed appimage/install-service.sh"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME cli_version COMMAND whisker --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "whisker ${PROJECT_VERSION}")
add_test(NAME cli_check_sample_config COMMAND whisker --check-config -c ${CMAKE_SOURCE_DIR}/config.yaml)
add_test(NAME cli_check_bad_config COMMAND whisker --check-config -c ${CMAKE_CURRENT_SOURCE_DIR}/data/bad-combo.yaml)
set_tests_properties(cli_check_bad_config PROPERTIES WILL_FAIL TRUE)

whisker_test(test_spawn ${CMAKE_SOURCE_DIR}/src/spawn.cpp)

add_test(NAME cli_unknown_flag
    COMMAND sh -c "$<TARGET_FILE:whisker> --settings-only; test $? -eq 2")
set_tests_properties(cli_unknown_flag PROPERTIES TIMEOUT 10)
add_test(NAME apprun_flags_known
    COMMAND sh -c "! grep 'usr/bin/whisker\"' appimage/AppRun | grep -oE -- '--[a-z-]+' | grep -vxE -- '--(settings|version|help|check-config)'"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
