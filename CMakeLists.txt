#
# eXellent Multi-platform emulator type 8 - 'XM8'
# based on ePC-8801MA
#
# Author (ePC-8801MA) : Takeda.Toshiya
# Author (XM8) : Tanaka.Yasushi
#
# [ CMakeLists for macOS/Linux ]
#

cmake_minimum_required(VERSION 3.15)

foreach(_policy CMP0111 CMP0126 CMP0135 CMP0175 CMP0177)
  if(POLICY ${_policy})
    cmake_policy(SET ${_policy} NEW)
    set(CMAKE_POLICY_DEFAULT_${_policy} NEW)
  endif()
endforeach()

# Projects added via `add_subdirectory` or `FetchContent` may have a lower
# `cmake_minimum_required` than we set here. Set policies that we require
# to their new value so that they still apply.
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
  message(FATAL_ERROR "DO NOT BUILD in-tree.")
endif()

include(CMakeDependentOption)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Builder/CMake")

# Get version from environment variable
set(PROJECT_VERSION 2.0.0)

# Project Name
project(XM8 VERSION ${PROJECT_VERSION})

if(APPLE)
  enable_language(OBJC OBJCXX)
endif()

include(CTest)

option(XM8_ENABLE_RETROACHIEVEMENTS "Enable RetroAchievements support" OFF)
option(XM8_ENABLE_DEBUGGER "Enable the development-only emulator debugger" OFF)
if(XM8_ENABLE_RETROACHIEVEMENTS AND XM8_ENABLE_DEBUGGER)
  message(FATAL_ERROR
    "XM8_ENABLE_DEBUGGER cannot be enabled in a RetroAchievements build")
endif()
if(XM8_ENABLE_DEBUGGER)
  add_compile_definitions(XM8_ENABLE_DEBUGGER=1 USE_DEBUGGER=1)
endif()

# Packaging options
option(MACOSX_STANDALONE_APP_BUNDLE "Generate a portable app bundle to use on other devices (requires sudo)" OFF)

include(MoldLinker)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
# set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clang-tidy
# set(CMAKE_THREAD_PREFER_PTHREAD ON)
# set(THREADS_PREFER_PTHREAD_FLAG ON)
# find_package(Threads REQUIRED)


# Dependencies must be included after Platforms.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
if(APPLE OR WIN32)
  # macOS/Windows: Build bundled SDL2 source by default.
  set(XM8_SYSTEM_SDL2 OFF CACHE BOOL "Use system-installed SDL2" FORCE)
  set(XM8_STATIC_SDL2 OFF CACHE BOOL "Link SDL2 statically" FORCE)
  message(STATUS "📦 SDL2: using bundled source")
else()
  # Linux: Use system-installed SDL2 by default
  set(XM8_SYSTEM_SDL2 ON CACHE BOOL "Use system-installed SDL2" FORCE)
  set(XM8_STATIC_SDL2 OFF CACHE BOOL "Link SDL2 statically" FORCE)
  message(STATUS "📦 SDL2: using system-installed library")
endif()
include(Dependencies)

# xBRZ
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Source/Filter/xBRZ)

if(XM8_ENABLE_RETROACHIEVEMENTS)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty)
endif()

# Set compile options
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall)
  add_compile_options(
    -Wno-macro-redefined
    -Wno-non-c-typedef-for-linkage
    -Wno-deprecated-declarations
    -Wno-overloaded-virtual
    -Wno-c++11-compat-deprecated-writable-strings
    -Wno-narrowing)
endif()

# Include directories
include_directories(
  Source/UI
  Source/ePC-8801MA
  Source/ePC-8801MA/vm
  Source/ePC-8801MA/vm/pc8801
  Source/ePC-8801MA/vm/fmgen
  Source/Filter/xBRZ
)

# Set source files
set(SRCS
  Source/UI/app.cpp
  Source/UI/audio.cpp
  Source/UI/clidisk.cpp
  Source/UI/converter.cpp
  Source/UI/d88probe.cpp
  Source/UI/diskmgr.cpp
  Source/UI/emu.cpp
  Source/UI/emu_sdl.cpp
  Source/UI/file.cpp
  Source/UI/fileio.cpp
  Source/UI/font.cpp
  Source/UI/input.cpp
  Source/UI/m3u.cpp
  Source/UI/main.cpp
  Source/UI/menu.cpp
  Source/UI/menuitem.cpp
  Source/UI/menulist.cpp
  Source/UI/pathresolver.cpp
  Source/UI/platform.cpp
  Source/UI/setting.cpp
  Source/UI/softkey.cpp
  Source/UI/tapemgr.cpp
  Source/UI/video.cpp
  Source/ePC-8801MA/common.cpp
  Source/ePC-8801MA/config.cpp
  Source/ePC-8801MA/fifo.cpp
  Source/ePC-8801MA/vm/disk.cpp
  Source/ePC-8801MA/vm/event.cpp
  Source/ePC-8801MA/vm/i8251.cpp
  Source/ePC-8801MA/vm/i8253.cpp
  Source/ePC-8801MA/vm/i8255.cpp
  Source/ePC-8801MA/vm/disksub.cpp
  Source/ePC-8801MA/vm/pcm1bit.cpp
  Source/ePC-8801MA/vm/upd765a.cpp
  Source/ePC-8801MA/vm/upd1990a.cpp
  Source/ePC-8801MA/vm/fmsound.cpp
  Source/ePC-8801MA/vm/z80.cpp
  Source/ePC-8801MA/vm/fmgen/opna.cpp
  Source/ePC-8801MA/vm/fmgen/psg.cpp
  Source/ePC-8801MA/vm/fmgen/fmgen.cpp
  Source/ePC-8801MA/vm/fmgen/fmtimer.cpp
  Source/ePC-8801MA/vm/pc8801/pc88.cpp
  Source/ePC-8801MA/vm/pc8801/pc8801.cpp
)

if(APPLE)
  list(APPEND SRCS
    Source/UI/converter_mac.mm
    Source/UI/pathresolver_mac.mm
  )
endif()

set(BIN_TARGET "xm8")
add_executable(${BIN_TARGET} MACOSX_BUNDLE ${SRCS} Builder/macOS/AppIcon.icns)
target_compile_definitions(${BIN_TARGET} PRIVATE SDL _PC8801MA) 
if(WIN32)
  # Match Builder/Windows/XM8.vcxproj's Unicode character set. platform.cpp's
  # Win32 file browser implementation uses the wide Win32 API behind UNICODE.
  target_compile_definitions(${BIN_TARGET} PRIVATE UNICODE _UNICODE)
  target_link_libraries(${BIN_TARGET} PRIVATE imm32)
endif()

set(XM8_COMMON_LICENSE_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/licenses/XM8-GPL-2.0.txt
  ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/licenses/SDL2-zlib.txt
  ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/licenses/xBRZ-GPL-3.0.txt
)
set(XM8_RA_LICENSE_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/licenses/rcheevos-MIT.txt
  ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/licenses/stb-MIT-or-Public-Domain.txt
  ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/licenses/SQLite-Public-Domain.txt
)
set(XM8_DISTRIBUTION_LICENSE_FILES
  ${XM8_COMMON_LICENSE_FILES}
  ${XM8_RA_LICENSE_FILES}
)
set(XM8_NOTICE_FILE
  ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/THIRD_PARTY_NOTICES.md)
set(XM8_PUBLIC_POLICY_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/PRIVACY.md
  ${CMAKE_CURRENT_SOURCE_DIR}/PRIVACY.ja.md
  ${CMAKE_CURRENT_SOURCE_DIR}/LICENSES.md)

set_source_files_properties(${XM8_DISTRIBUTION_LICENSE_FILES} PROPERTIES
  MACOSX_PACKAGE_LOCATION "Resources/Licenses")
set_source_files_properties(${XM8_NOTICE_FILE} PROPERTIES
  MACOSX_PACKAGE_LOCATION "Resources")
set_source_files_properties(${XM8_PUBLIC_POLICY_FILES} PROPERTIES
  MACOSX_PACKAGE_LOCATION "Resources")
target_sources(${BIN_TARGET} PRIVATE ${XM8_DISTRIBUTION_LICENSE_FILES}
  ${XM8_NOTICE_FILE} ${XM8_PUBLIC_POLICY_FILES})

if(XM8_ENABLE_RETROACHIEVEMENTS)
  xm8_enable_ra_for_target(${BIN_TARGET})
endif()

if(CMAKE_STRIP)
  add_custom_command(
    TARGET ${BIN_TARGET} POST_BUILD
    COMMAND $<$<CONFIG:Release>:${CMAKE_STRIP}>
            $<$<CONFIG:MinSizeRel>:${CMAKE_STRIP}>
            $<TARGET_FILE:${BIN_TARGET}>)
endif()

if(APPLE)
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Builder/macOS/AppIcon.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(${BIN_TARGET} PROPERTIES
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.bubio.xm8"
      MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2015 PI."
      MACOSX_BUNDLE_BUNDLE_NAME "XM8M"
      MACOSX_BUNDLE_DISPLAY_NAME "XM8M"
      MACOSX_BUNDLE_INFO_STRING ${PROJECT_VERSION}
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
      MACOSX_BUNDLE_LONG_VERSION_STRING "Version ${PROJECT_VERSION}"
      MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Builder/macOS/Info.plist"
  )

  install (TARGETS ${BIN_TARGET} DESTINATION ./)

  if(MACOSX_STANDALONE_APP_BUNDLE)
      install(CODE "
        include(BundleUtilities)
        fixup_bundle(${CMAKE_BINARY_DIR}/${BIN_TARGET}.app \"\" \"\")
        "
        COMPONENT Runtime)
  endif()

endif()

option(ENABLE_PACKAGING "Enable CPack packaging" ON)

# Linux desktop integration.  Flatpak passes its reverse-DNS application ID
# here so that the installed desktop file and icon have the names it expects.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  string(TOLOWER ${PROJECT_NAME} project_name)
  set(XM8_DESKTOP_FILE
    "${PROJECT_SOURCE_DIR}/Builder/Linux/${project_name}.desktop"
    CACHE FILEPATH "Desktop entry to install")
  set(XM8_DESKTOP_FILE_NAME "${project_name}.desktop"
    CACHE STRING "Installed desktop entry file name")
  set(XM8_ICON_FILE_NAME "${project_name}.png"
    CACHE STRING "Installed application icon file name")

  install(TARGETS ${BIN_TARGET} DESTINATION bin)
  install(FILES "${XM8_DESKTOP_FILE}"
    DESTINATION "share/applications"
    RENAME "${XM8_DESKTOP_FILE_NAME}")
  install(FILES "${PROJECT_SOURCE_DIR}/Builder/Linux/icon.png"
    DESTINATION "share/icons/hicolor/512x512/apps"
    RENAME "${XM8_ICON_FILE_NAME}")
  install(FILES
    "${PROJECT_SOURCE_DIR}/Builder/Linux/Flatpak/com.github.bubio.xm8m.metainfo.xml"
    DESTINATION "share/metainfo")
  install(FILES ${XM8_PUBLIC_POLICY_FILES} ${XM8_NOTICE_FILE}
    DESTINATION "share/doc/xm8m")
  install(FILES ${XM8_DISTRIBUTION_LICENSE_FILES}
    DESTINATION "share/doc/xm8m/Licenses")
endif()

# Link SDL2 and xBRZ to xm8 target using interface target from Dependencies.cmake
target_link_libraries(${BIN_TARGET} PRIVATE XM8::SDL xbrz)
if(APPLE)
  target_link_libraries(${BIN_TARGET} PRIVATE "-framework Foundation")
  find_library(XM8_ICONV_LIBRARY iconv REQUIRED)
  target_link_libraries(${BIN_TARGET} PRIVATE ${XM8_ICONV_LIBRARY})
endif()

if(BUILD_TESTING)
  if(XM8_ENABLE_RETROACHIEVEMENTS)
    # Exercise the real App media entry points and VM without opening a window
    # or resolving the user's ROM/configuration directory.
    set(XM8_APP_MEDIA_TEST_SRCS ${SRCS})
    list(REMOVE_ITEM XM8_APP_MEDIA_TEST_SRCS
      Source/UI/main.cpp Source/UI/emu_sdl.cpp)
    add_executable(ra_app_media_integration_test
      Tests/ra_app_media_integration_test.cpp
      Tests/Fixtures/d88_fixture.cpp
      Source/RA/ra_http_fake.cpp
      ${XM8_APP_MEDIA_TEST_SRCS})
    target_compile_definitions(ra_app_media_integration_test PRIVATE SDL _PC8801MA)
    target_include_directories(ra_app_media_integration_test PRIVATE Tests)
    target_link_libraries(ra_app_media_integration_test PRIVATE XM8::SDL xbrz)
    xm8_enable_ra_for_target(ra_app_media_integration_test)
    if(APPLE)
      target_link_libraries(ra_app_media_integration_test PRIVATE
        "-framework Foundation" ${XM8_ICONV_LIBRARY})
    endif()
    if(WIN32)
      target_compile_definitions(ra_app_media_integration_test PRIVATE UNICODE _UNICODE)
      target_link_libraries(ra_app_media_integration_test PRIVATE imm32)
    endif()
    add_test(NAME ra_app_media_integration_test COMMAND ra_app_media_integration_test)
  endif()

  add_executable(clidisk_test
    Tests/clidisk_test.cpp
    Source/UI/clidisk.cpp
    Source/UI/m3u.cpp
  )
  target_include_directories(clidisk_test PRIVATE Source/UI)
  add_test(NAME clidisk_test COMMAND clidisk_test)

  add_executable(menu_file_routing_test
    Tests/menu_file_routing_test.cpp
  )
  target_include_directories(menu_file_routing_test PRIVATE Source/UI)
  add_test(NAME menu_file_routing_test COMMAND menu_file_routing_test)

  add_executable(d88probe_test
    Tests/d88probe_test.cpp
    Source/UI/d88probe.cpp
    Source/UI/fileio.cpp
  )
  target_include_directories(d88probe_test PRIVATE
    Source/UI
    Source/ePC-8801MA
  )
  target_compile_definitions(d88probe_test PRIVATE SDL _PC8801MA)
  target_link_libraries(d88probe_test PRIVATE XM8::SDL)
  add_test(NAME d88probe_test COMMAND d88probe_test)

  add_executable(generate_d88_fixtures
    Tests/generate_d88_fixtures.cpp
    Tests/Fixtures/d88_fixture.cpp
  )
  target_include_directories(generate_d88_fixtures PRIVATE Tests)

  add_executable(d88fixture_test
    Tests/d88fixture_test.cpp
    Tests/Fixtures/d88_fixture.cpp
    Source/UI/d88probe.cpp
    Source/UI/fileio.cpp
    Source/UI/m3u.cpp
  )
  target_include_directories(d88fixture_test PRIVATE
    Tests
    Source/UI
    Source/ePC-8801MA
  )
  target_compile_definitions(d88fixture_test PRIVATE SDL _PC8801MA)
  target_link_libraries(d88fixture_test PRIVATE XM8::SDL)
  add_test(NAME d88fixture_test COMMAND d88fixture_test)

  add_executable(d88write_test
    Tests/d88write_test.cpp
    Tests/Fixtures/d88_fixture.cpp
    Source/UI/fileio.cpp
    Source/ePC-8801MA/common.cpp
    Source/ePC-8801MA/config.cpp
    Source/ePC-8801MA/vm/disk.cpp
  )
  target_include_directories(d88write_test PRIVATE
    Tests
    Source/UI
    Source/ePC-8801MA
    Source/ePC-8801MA/vm
  )
  target_compile_definitions(d88write_test PRIVATE SDL _PC8801MA)
  target_link_libraries(d88write_test PRIVATE XM8::SDL)
  add_test(NAME d88write_test COMMAND d88write_test)

  add_executable(pathresolver_test
    Tests/pathresolver_test.cpp
    Source/UI/pathresolver.cpp
  )
  if(APPLE)
    target_sources(pathresolver_test PRIVATE Source/UI/pathresolver_mac.mm)
    target_link_libraries(pathresolver_test PRIVATE "-framework Foundation")
  endif()
  target_include_directories(pathresolver_test PRIVATE Source/UI)
  add_test(NAME pathresolver_test COMMAND pathresolver_test)

  add_executable(host_frame_callback_test
    Tests/host_frame_callback_test.cpp
  )
  target_include_directories(host_frame_callback_test PRIVATE
    Source/ePC-8801MA/vm
  )
  add_test(NAME host_frame_callback_test COMMAND host_frame_callback_test)

  add_executable(event_host_frame_integration_test
    Tests/event_host_frame_integration_test.cpp
    Tests/Fixtures/d88_fixture.cpp
    Source/UI/d88probe.cpp
    Source/UI/diskmgr.cpp
    Source/UI/file.cpp
    Source/UI/fileio.cpp
    Source/UI/pathresolver.cpp
    Source/ePC-8801MA/common.cpp
    Source/ePC-8801MA/config.cpp
    Source/ePC-8801MA/fifo.cpp
    Source/ePC-8801MA/vm/disk.cpp
    Source/ePC-8801MA/vm/event.cpp
    Source/ePC-8801MA/vm/i8251.cpp
    Source/ePC-8801MA/vm/i8253.cpp
    Source/ePC-8801MA/vm/i8255.cpp
    Source/ePC-8801MA/vm/disksub.cpp
    Source/ePC-8801MA/vm/pcm1bit.cpp
    Source/ePC-8801MA/vm/upd765a.cpp
    Source/ePC-8801MA/vm/upd1990a.cpp
    Source/ePC-8801MA/vm/fmsound.cpp
    Source/ePC-8801MA/vm/z80.cpp
    Source/ePC-8801MA/vm/fmgen/opna.cpp
    Source/ePC-8801MA/vm/fmgen/psg.cpp
    Source/ePC-8801MA/vm/fmgen/fmgen.cpp
    Source/ePC-8801MA/vm/fmgen/fmtimer.cpp
    Source/ePC-8801MA/vm/pc8801/pc88.cpp
    Source/ePC-8801MA/vm/pc8801/pc8801.cpp
  )
  target_include_directories(event_host_frame_integration_test PRIVATE
    Tests
    Source/UI
    Source/ePC-8801MA
    Source/ePC-8801MA/vm
    Source/ePC-8801MA/vm/pc8801
    Source/ePC-8801MA/vm/fmgen
  )
  target_compile_definitions(event_host_frame_integration_test PRIVATE
    SDL _PC8801MA)
  target_link_libraries(event_host_frame_integration_test PRIVATE XM8::SDL)
  if(APPLE)
    target_sources(event_host_frame_integration_test PRIVATE
      Source/UI/pathresolver_mac.mm)
    target_link_libraries(event_host_frame_integration_test PRIVATE
      "-framework Foundation")
  endif()
  add_test(NAME event_host_frame_integration_test
    COMMAND event_host_frame_integration_test)

  add_executable(fileio_error_test
    Tests/fileio_error_test.cpp
    Source/UI/fileio.cpp
  )
  target_include_directories(fileio_error_test PRIVATE
    Source/UI
    Source/ePC-8801MA
  )
  target_compile_definitions(fileio_error_test PRIVATE SDL)
  target_link_libraries(fileio_error_test PRIVATE XM8::SDL)
  add_test(NAME fileio_error_test COMMAND fileio_error_test)

  if(APPLE)
    add_executable(converter_mac_test
      Tests/converter_mac_test.mm
      Source/UI/converter_mac.mm
    )
    target_include_directories(converter_mac_test PRIVATE Source/UI)
    target_link_libraries(converter_mac_test PRIVATE "-framework Foundation")
    add_test(NAME converter_mac_test COMMAND converter_mac_test)

    add_executable(pathresolver_mac_test
      Tests/pathresolver_mac_test.mm
      Source/UI/pathresolver.cpp
      Source/UI/pathresolver_mac.mm
    )
    target_include_directories(pathresolver_mac_test PRIVATE Source/UI)
    target_link_libraries(pathresolver_mac_test PRIVATE "-framework Foundation")
    add_test(NAME pathresolver_mac_test COMMAND pathresolver_mac_test)
  endif()

  if(XM8_ENABLE_RETROACHIEVEMENTS)
	find_package(Threads REQUIRED)
    set(RA_PATHRESOLVER_SOURCES
      Source/UI/pathresolver.cpp
    )
    if(APPLE)
      list(APPEND RA_PATHRESOLVER_SOURCES Source/UI/pathresolver_mac.mm)
    endif()

    add_executable(ra_dependency_test
      Tests/ra_dependency_test.cpp
      Source/UI/m3u.cpp
      ${RA_PATHRESOLVER_SOURCES}
      $<TARGET_OBJECTS:xm8_ra_core>
      $<TARGET_OBJECTS:xm8_ra_platform>
      $<TARGET_OBJECTS:xm8_ra_media_xm8>
    )
    target_link_libraries(ra_dependency_test PRIVATE xm8_ra_dependencies)
    target_include_directories(ra_dependency_test PRIVATE Source/RA Source/UI)
    target_compile_definitions(ra_dependency_test PRIVATE
      XM8_ENABLE_RETROACHIEVEMENTS=1)
    add_test(NAME ra_dependency_test COMMAND ra_dependency_test)

    add_executable(ra_media_probe_test
      Tests/ra_media_probe_test.cpp
      Tests/Fixtures/d88_fixture.cpp
      Source/UI/m3u.cpp
      ${RA_PATHRESOLVER_SOURCES}
      $<TARGET_OBJECTS:xm8_ra_core>
      $<TARGET_OBJECTS:xm8_ra_platform>
      $<TARGET_OBJECTS:xm8_ra_media_xm8>
    )
    target_link_libraries(ra_media_probe_test PRIVATE xm8_ra_dependencies)
    target_include_directories(ra_media_probe_test PRIVATE
      Tests
      Source/RA
      Source/UI
    )
    target_compile_definitions(ra_media_probe_test PRIVATE
      XM8_ENABLE_RETROACHIEVEMENTS=1)
    add_test(NAME ra_media_probe_test COMMAND ra_media_probe_test)

    add_executable(ra_media_change_policy_test
      Tests/ra_media_change_policy_test.cpp
    )
    target_include_directories(ra_media_change_policy_test PRIVATE Source/RA)
    add_test(NAME ra_media_change_policy_test COMMAND ra_media_change_policy_test)

    add_executable(ra_disk_transaction_test
      Tests/ra_disk_transaction_test.cpp
    )
    target_include_directories(ra_disk_transaction_test PRIVATE Source/RA)
    add_test(NAME ra_disk_transaction_test COMMAND ra_disk_transaction_test)

    add_executable(ra_media_request_integration_test
      Tests/ra_media_request_integration_test.cpp
    )
    target_include_directories(ra_media_request_integration_test PRIVATE Source/RA)
    add_test(NAME ra_media_request_integration_test
      COMMAND ra_media_request_integration_test)

    add_executable(ra_drop_session_scenario_test
      Tests/ra_drop_session_scenario_test.cpp
    )
    target_include_directories(ra_drop_session_scenario_test PRIVATE Source/RA)
    add_test(NAME ra_drop_session_scenario_test
      COMMAND ra_drop_session_scenario_test)

    add_executable(ra_media_operation_test Tests/ra_media_operation_test.cpp)
    target_include_directories(ra_media_operation_test PRIVATE Source/RA)
    add_test(NAME ra_media_operation_test COMMAND ra_media_operation_test)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      add_test(NAME ra_media_operation_generation_test
        COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/Documents/RetroAchievements/48_generate_media_machine.py
          --check --self-test)
    endif()

    add_executable(ra_media_state_matrix_test
      Tests/ra_media_state_matrix_test.cpp
    )
    target_include_directories(ra_media_state_matrix_test PRIVATE Source/RA)
    add_test(NAME ra_media_state_matrix_test
      COMMAND ra_media_state_matrix_test)
    add_test(NAME verify_ra_script_test
      COMMAND bash ${CMAKE_SOURCE_DIR}/Tests/verify_ra_script_test.sh
        ${CMAKE_SOURCE_DIR}/scripts/verify_ra.sh)

    add_executable(ra_library_launch_policy_test
      Tests/ra_library_launch_policy_test.cpp
    )
    target_include_directories(ra_library_launch_policy_test PRIVATE Source/RA)
    add_test(NAME ra_library_launch_policy_test
      COMMAND ra_library_launch_policy_test)

    add_executable(ra_auxiliary_mount_commit_policy_test
      Tests/ra_auxiliary_mount_commit_policy_test.cpp
    )
    target_include_directories(ra_auxiliary_mount_commit_policy_test PRIVATE
      Source/RA)
    add_test(NAME ra_auxiliary_mount_commit_policy_test
      COMMAND ra_auxiliary_mount_commit_policy_test)

    add_executable(ra_multi_image_policy_test
      Tests/ra_multi_image_policy_test.cpp
    )
    target_include_directories(ra_multi_image_policy_test PRIVATE Source/RA)
    add_test(NAME ra_multi_image_policy_test
      COMMAND ra_multi_image_policy_test)

    add_executable(ra_media_eject_policy_test
      Tests/ra_media_eject_policy_test.cpp
    )
    target_include_directories(ra_media_eject_policy_test PRIVATE Source/RA)
    add_test(NAME ra_media_eject_policy_test
      COMMAND ra_media_eject_policy_test)

	add_executable(ra_callback_lifetime_test
	  Tests/ra_callback_lifetime_test.cpp
	)
	target_include_directories(ra_callback_lifetime_test PRIVATE Source/RA)
	target_link_libraries(ra_callback_lifetime_test PRIVATE Threads::Threads)
	add_test(NAME ra_callback_lifetime_test COMMAND ra_callback_lifetime_test)

	add_executable(ra_leaderboard_fetch_policy_test
	  Tests/ra_leaderboard_fetch_policy_test.cpp
	)
	target_include_directories(ra_leaderboard_fetch_policy_test PRIVATE Source/RA)
	add_test(NAME ra_leaderboard_fetch_policy_test
	  COMMAND ra_leaderboard_fetch_policy_test)

    add_executable(ra_session_state_test
      Tests/ra_session_state_test.cpp
    )
    target_include_directories(ra_session_state_test PRIVATE Source/RA)
    add_test(NAME ra_session_state_test COMMAND ra_session_state_test)

    add_executable(ra_menu_status_test
      Tests/ra_menu_status_test.cpp
    )
    target_include_directories(ra_menu_status_test PRIVATE Source/RA)
    add_test(NAME ra_menu_status_test COMMAND ra_menu_status_test)

    add_executable(ra_session_policy_test
      Tests/ra_session_policy_test.cpp
    )
    target_include_directories(ra_session_policy_test PRIVATE Source/RA)
    add_test(NAME ra_session_policy_test COMMAND ra_session_policy_test)

    add_executable(ra_user_agent_test Tests/ra_user_agent_test.cpp)
    target_include_directories(ra_user_agent_test PRIVATE Source/RA)
    add_test(NAME ra_user_agent_test COMMAND ra_user_agent_test)

    add_executable(ra_state_store_test
      Tests/ra_state_store_test.cpp
      Source/RA/ra_file_util.cpp
      Source/RA/ra_state_store.cpp
    )
    target_include_directories(ra_state_store_test PRIVATE Source/RA)
    add_test(NAME ra_state_store_test COMMAND ra_state_store_test)

    add_executable(ra_file_util_test
      Tests/ra_file_util_test.cpp
      Source/RA/ra_file_util.cpp
    )
    target_include_directories(ra_file_util_test PRIVATE Source/RA)
    add_test(NAME ra_file_util_test COMMAND ra_file_util_test)

    add_executable(ra_inspect_media
      Tests/ra_inspect_media.cpp
      Source/UI/m3u.cpp
      ${RA_PATHRESOLVER_SOURCES}
      $<TARGET_OBJECTS:xm8_ra_core>
      $<TARGET_OBJECTS:xm8_ra_platform>
      $<TARGET_OBJECTS:xm8_ra_media_xm8>
    )
    target_link_libraries(ra_inspect_media PRIVATE xm8_ra_dependencies)
    target_include_directories(ra_inspect_media PRIVATE
      Source/RA
      Source/UI
    )
    target_compile_definitions(ra_inspect_media PRIVATE
      XM8_ENABLE_RETROACHIEVEMENTS=1)

    add_executable(ra_memory_map_test
      Tests/ra_memory_map_test.cpp
    )
    target_include_directories(ra_memory_map_test PRIVATE
      Source/ePC-8801MA
      Source/ePC-8801MA/vm
      Source/ePC-8801MA/vm/pc8801
    )
    target_compile_definitions(ra_memory_map_test PRIVATE
      SDL _PC8801MA XM8_ENABLE_RETROACHIEVEMENTS=1)
    add_test(NAME ra_memory_map_test COMMAND ra_memory_map_test)

    add_executable(ra_library_store_test
      Tests/ra_library_store_test.cpp
      Tests/Fixtures/d88_fixture.cpp
      Source/UI/m3u.cpp
      ${RA_PATHRESOLVER_SOURCES}
      $<TARGET_OBJECTS:xm8_ra_core>
      $<TARGET_OBJECTS:xm8_ra_platform>
      $<TARGET_OBJECTS:xm8_ra_media_xm8>
    )
    target_link_libraries(ra_library_store_test PRIVATE xm8_ra_dependencies)
    target_include_directories(ra_library_store_test PRIVATE
      Tests
      Source/RA
      Source/UI
    )
    target_compile_definitions(ra_library_store_test PRIVATE
      XM8_ENABLE_RETROACHIEVEMENTS=1)
    add_test(NAME ra_library_store_test COMMAND ra_library_store_test)

    add_executable(ra_seed_library_fixture
      Tests/ra_seed_library_fixture.cpp
      Tests/Fixtures/d88_fixture.cpp
      Tests/Fixtures/ra_library_fixture_seed.cpp
      Source/UI/m3u.cpp
      ${RA_PATHRESOLVER_SOURCES}
      $<TARGET_OBJECTS:xm8_ra_core>
      $<TARGET_OBJECTS:xm8_ra_platform>
      $<TARGET_OBJECTS:xm8_ra_media_xm8>
    )
    target_link_libraries(ra_seed_library_fixture PRIVATE xm8_ra_dependencies)
    target_include_directories(ra_seed_library_fixture PRIVATE
      Tests
      Source/RA
      Source/UI
    )
    target_compile_definitions(ra_seed_library_fixture PRIVATE
      XM8_ENABLE_RETROACHIEVEMENTS=1)

    add_executable(ra_seed_library_fixture_test
      Tests/ra_seed_library_fixture_test.cpp
      Tests/Fixtures/d88_fixture.cpp
      Tests/Fixtures/ra_library_fixture_seed.cpp
      Source/UI/m3u.cpp
      ${RA_PATHRESOLVER_SOURCES}
      $<TARGET_OBJECTS:xm8_ra_core>
      $<TARGET_OBJECTS:xm8_ra_platform>
      $<TARGET_OBJECTS:xm8_ra_media_xm8>
    )
    target_link_libraries(ra_seed_library_fixture_test PRIVATE
      xm8_ra_dependencies)
    target_include_directories(ra_seed_library_fixture_test PRIVATE
      Tests
      Source/RA
      Source/UI
    )
    target_compile_definitions(ra_seed_library_fixture_test PRIVATE
      XM8_ENABLE_RETROACHIEVEMENTS=1)
    add_test(NAME ra_seed_library_fixture_test
      COMMAND ra_seed_library_fixture_test)

    add_executable(ra_working_media_identity_test
      Tests/ra_working_media_identity_test.cpp
      Tests/Fixtures/d88_fixture.cpp
      Tests/Fixtures/ra_library_fixture_seed.cpp
      Source/UI/m3u.cpp
      ${RA_PATHRESOLVER_SOURCES}
      $<TARGET_OBJECTS:xm8_ra_core>
      $<TARGET_OBJECTS:xm8_ra_platform>
      $<TARGET_OBJECTS:xm8_ra_media_xm8>
    )
    target_link_libraries(ra_working_media_identity_test PRIVATE
      xm8_ra_dependencies)
    target_include_directories(ra_working_media_identity_test PRIVATE
      Tests
      Source/RA
      Source/UI
    )
    target_compile_definitions(ra_working_media_identity_test PRIVATE
      XM8_ENABLE_RETROACHIEVEMENTS=1)
    add_test(NAME ra_working_media_identity_test
      COMMAND ra_working_media_identity_test)

    add_executable(ra_credentials_http_test
      Tests/ra_credentials_http_test.cpp
      Source/RA/ra_http_fake.cpp
      Source/UI/m3u.cpp
      ${RA_PATHRESOLVER_SOURCES}
      $<TARGET_OBJECTS:xm8_ra_core>
      $<TARGET_OBJECTS:xm8_ra_platform>
      $<TARGET_OBJECTS:xm8_ra_media_xm8>
    )
    target_link_libraries(ra_credentials_http_test PRIVATE xm8_ra_dependencies)
    target_include_directories(ra_credentials_http_test PRIVATE
      Source/RA
      Source/UI
    )
    target_compile_definitions(ra_credentials_http_test PRIVATE
      XM8_ENABLE_RETROACHIEVEMENTS=1)
    add_test(NAME ra_credentials_http_test COMMAND ra_credentials_http_test)
    set_tests_properties(ra_credentials_http_test PROPERTIES
      ENVIRONMENT "XM8_RA_CREDENTIALS_TEST_MEMORY=1")

    if(WIN32)
      add_executable(ra_credentials_win_test
        Tests/ra_credentials_win_test.cpp
        $<TARGET_OBJECTS:xm8_ra_core>
        $<TARGET_OBJECTS:xm8_ra_platform>
      )
      target_link_libraries(ra_credentials_win_test PRIVATE
        xm8_ra_dependencies)
      target_include_directories(ra_credentials_win_test PRIVATE Source/RA)
      target_compile_definitions(ra_credentials_win_test PRIVATE
        XM8_ENABLE_RETROACHIEVEMENTS=1)
      add_test(NAME ra_credentials_win_test COMMAND ra_credentials_win_test)

      add_executable(ra_http_win_test Tests/ra_http_win_test.cpp
        $<TARGET_OBJECTS:xm8_ra_core>
        $<TARGET_OBJECTS:xm8_ra_platform>)
      target_link_libraries(ra_http_win_test PRIVATE
        xm8_ra_dependencies ws2_32)
      target_include_directories(ra_http_win_test PRIVATE Source/RA)
      target_compile_definitions(ra_http_win_test PRIVATE
        XM8_ENABLE_RETROACHIEVEMENTS=1)
      add_test(NAME ra_http_win_test COMMAND ra_http_win_test)
    elseif(UNIX AND NOT APPLE AND NOT ANDROID)
      add_executable(ra_http_linux_test Tests/ra_http_linux_test.cpp
        $<TARGET_OBJECTS:xm8_ra_core>
        $<TARGET_OBJECTS:xm8_ra_platform>)
      target_link_libraries(ra_http_linux_test PRIVATE xm8_ra_dependencies)
      target_include_directories(ra_http_linux_test PRIVATE Source/RA)
      target_compile_definitions(ra_http_linux_test PRIVATE
        XM8_ENABLE_RETROACHIEVEMENTS=1)
      add_test(NAME ra_http_linux_test COMMAND ra_http_linux_test)
    endif()

    add_executable(ra_service_test
      Tests/ra_service_test.cpp
      Source/RA/ra_http_fake.cpp
      $<TARGET_OBJECTS:xm8_ra_core>
      $<TARGET_OBJECTS:xm8_ra_platform>
    )
    target_link_libraries(ra_service_test PRIVATE xm8_ra_dependencies)
    target_include_directories(ra_service_test PRIVATE
      Source/RA
      Source/UI
    )
    target_compile_definitions(ra_service_test PRIVATE
      XM8_ENABLE_RETROACHIEVEMENTS=1)
    add_test(NAME ra_service_test COMMAND ra_service_test)
    set_tests_properties(ra_service_test PROPERTIES
      ENVIRONMENT "XM8_RA_CREDENTIALS_TEST_MEMORY=1")

    add_executable(ra_overlay_test
      Tests/ra_overlay_test.cpp
      Source/RA/ra_overlay.cpp
    )
    target_include_directories(ra_overlay_test PRIVATE Source/RA)
    target_compile_definitions(ra_overlay_test PRIVATE
      XM8_ENABLE_RETROACHIEVEMENTS=1)
    add_test(NAME ra_overlay_test COMMAND ra_overlay_test)

    add_executable(ra_text_converter_test
      Tests/ra_text_converter_test.cpp
      Source/RA/ra_text_converter.cpp
    )
    target_include_directories(ra_text_converter_test PRIVATE Source/RA)
    target_compile_definitions(ra_text_converter_test PRIVATE
      XM8_ENABLE_RETROACHIEVEMENTS=1)
    add_test(NAME ra_text_converter_test COMMAND ra_text_converter_test)
  endif()

  if(APPLE)
    # Every test/tool that compiles m3u.cpp uses iconv for CP932 conversion.
    # Keep this list centralized so clean macOS builds cannot accidentally
    # inherit the dependency from an unrelated target or stale build output.
    foreach(XM8_M3U_TARGET
        clidisk_test
        d88fixture_test
        ra_dependency_test
        ra_media_probe_test
        ra_inspect_media
        ra_library_store_test
        ra_seed_library_fixture
        ra_seed_library_fixture_test
        ra_working_media_identity_test
        ra_credentials_http_test)
      if(TARGET ${XM8_M3U_TARGET})
        target_link_libraries(${XM8_M3U_TARGET} PRIVATE ${XM8_ICONV_LIBRARY})
      endif()
    endforeach()
  endif()

  if(WIN32 AND TARGET SDL2::SDL2)
    foreach(SDL_TEST_TARGET
        d88probe_test
        d88fixture_test
        d88write_test
        event_host_frame_integration_test
        fileio_error_test)
      add_custom_command(TARGET ${SDL_TEST_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:SDL2::SDL2>
          $<TARGET_FILE_DIR:${SDL_TEST_TARGET}>
      )
    endforeach()
  endif()
endif()

# Packaging                      
if(ENABLE_PACKAGING)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_PACKAGE_NAME ${project_name})
    set(CPACK_GENERATOR "7Z")
    # Common *nix files
    set(CPACK_STRIP_FILES TRUE)
    # -G DEB
    set(CPACK_PACKAGE_CONTACT "bubio")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "PC-8801MA Emulator\neXellent Multi-platform emulator type 8 - 'XM8' based on ePC-8801MA")
    if(XM8_ENABLE_RETROACHIEVEMENTS)
      set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libsdl2-2.0-0, libcurl4, libsecret-1-0")
    else()
      set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsdl2-2.0-0")
    endif()
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

    # -G RPM
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    if(XM8_ENABLE_RETROACHIEVEMENTS)
      set(CPACK_RPM_PACKAGE_REQUIRES "SDL2, libcurl, libsecret")
    else()
      set(CPACK_RPM_PACKAGE_REQUIRES "SDL2")
    endif()

    find_program(RPMBUILD rpmbuild)
    if(RPMBUILD)
      list(APPEND CPACK_GENERATOR "RPM")
    endif()
    find_program(DPKG dpkg)
    if(DPKG)
      list(APPEND CPACK_GENERATOR "DEB")
    endif()

  elseif(APPLE)
    set(CPACK_PACKAGE_FILE_NAME "XM8")
    set(CPACK_DMG_DISABLE_APPLICATIONS_SYMLINK "ON")
    set(CPACK_STRIP_FILES TRUE)
    set(CPACK_GENERATOR "DragNDrop")
  endif()

  set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
  set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
  set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
  include(CPack)
endif()
