set(XM8_RCHEEVOS_SOURCES
  rcheevos/src/rc_client.c
  rcheevos/src/rc_client_raintegration.c
  rcheevos/src/rc_compat.c
  rcheevos/src/rc_util.c
  rcheevos/src/rc_version.c
  rcheevos/src/rcheevos/alloc.c
  rcheevos/src/rcheevos/condition.c
  rcheevos/src/rcheevos/condset.c
  rcheevos/src/rcheevos/consoleinfo.c
  rcheevos/src/rcheevos/format.c
  rcheevos/src/rcheevos/lboard.c
  rcheevos/src/rcheevos/memref.c
  rcheevos/src/rcheevos/operand.c
  rcheevos/src/rcheevos/rc_validate.c
  rcheevos/src/rcheevos/richpresence.c
  rcheevos/src/rcheevos/runtime.c
  rcheevos/src/rcheevos/runtime_progress.c
  rcheevos/src/rcheevos/trigger.c
  rcheevos/src/rcheevos/value.c
  rcheevos/src/rapi/rc_api_common.c
  rcheevos/src/rapi/rc_api_editor.c
  rcheevos/src/rapi/rc_api_info.c
  rcheevos/src/rapi/rc_api_runtime.c
  rcheevos/src/rapi/rc_api_user.c
  rcheevos/src/rhash/aes.c
  rcheevos/src/rhash/cdreader.c
  rcheevos/src/rhash/hash.c
  rcheevos/src/rhash/hash_disc.c
  rcheevos/src/rhash/hash_encrypted.c
  rcheevos/src/rhash/hash_rom.c
  rcheevos/src/rhash/hash_zip.c
  rcheevos/src/rhash/md5.c
)

add_library(xm8_rcheevos STATIC ${XM8_RCHEEVOS_SOURCES})
target_include_directories(xm8_rcheevos
  PUBLIC rcheevos/include
  PRIVATE rcheevos/src rcheevos/src/rcheevos rcheevos/src/rapi rcheevos/src/rhash
)
target_compile_definitions(xm8_rcheevos PUBLIC
  RC_DISABLE_LUA
  RC_CLIENT_SUPPORTS_HASH
)

add_library(xm8_sqlite STATIC sqlite/sqlite3.c)
target_include_directories(xm8_sqlite PUBLIC sqlite)
target_compile_definitions(xm8_sqlite PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_DEFAULT_FOREIGN_KEYS=1
  SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_DQS=0
)

add_library(xm8_stb STATIC ${PROJECT_SOURCE_DIR}/Source/RA/ra_stb_image.cpp)
target_include_directories(xm8_stb PUBLIC stb)
target_compile_definitions(xm8_stb PRIVATE
  STB_IMAGE_IMPLEMENTATION
  STBI_ONLY_PNG
  STBI_ONLY_JPEG
  STBI_NO_STDIO
  STBI_NO_HDR
  STBI_NO_LINEAR
  STBI_MAX_DIMENSIONS=2048
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(xm8_rcheevos PRIVATE -w)
  target_compile_options(xm8_sqlite PRIVATE -w)
  target_compile_options(xm8_stb PRIVATE -w)
endif()

add_library(xm8_ra_dependencies INTERFACE)
target_link_libraries(xm8_ra_dependencies INTERFACE
  xm8_platform_path
  xm8_rcheevos
  xm8_sqlite
  xm8_stb
)

add_library(xm8_ra_core OBJECT
	${PROJECT_SOURCE_DIR}/Source/RA/ra_overlay.cpp
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_paths.cpp
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_rc_client_http.cpp
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_service.cpp
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_state_store.cpp
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_text_converter.cpp
)

add_library(xm8_ra_platform OBJECT
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_file_util.cpp
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_credentials.cpp
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_platform.cpp
)
if(WIN32)
  target_sources(xm8_ra_platform PRIVATE
    ${PROJECT_SOURCE_DIR}/Source/RA/ra_connectivity_win.cpp
    ${PROJECT_SOURCE_DIR}/Source/RA/ra_http_win.cpp
  )
  target_compile_definitions(xm8_ra_platform PRIVATE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
  )
elseif(APPLE)
  target_sources(xm8_ra_platform PRIVATE
    ${PROJECT_SOURCE_DIR}/Source/RA/ra_connectivity_mac.cpp
    ${PROJECT_SOURCE_DIR}/Source/RA/ra_http_mac.mm
  )
elseif(UNIX AND NOT ANDROID)
  target_sources(xm8_ra_platform PRIVATE
    ${PROJECT_SOURCE_DIR}/Source/RA/ra_connectivity_linux.cpp
    ${PROJECT_SOURCE_DIR}/Source/RA/ra_http_linux.cpp
  )
endif()

add_library(xm8_ra_media_xm8 OBJECT
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_build_info.cpp
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_library.cpp
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_media_probe.cpp
  ${PROJECT_SOURCE_DIR}/Source/RA/ra_media_store.cpp
)

foreach(_ra_target xm8_ra_core xm8_ra_platform xm8_ra_media_xm8)
  target_include_directories(${_ra_target}
    PUBLIC ${PROJECT_SOURCE_DIR}/Source/RA
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rcheevos/src
  )
  target_link_libraries(${_ra_target} PRIVATE xm8_ra_dependencies)
  target_compile_definitions(${_ra_target} PUBLIC XM8_ENABLE_RETROACHIEVEMENTS=1)
endforeach()

target_include_directories(xm8_ra_media_xm8
  PUBLIC ${PROJECT_SOURCE_DIR}/Source/RA
  PRIVATE
    ${PROJECT_SOURCE_DIR}/Source/UI
    ${CMAKE_CURRENT_SOURCE_DIR}/rcheevos/src
)
if(WIN32)
  target_link_libraries(xm8_ra_dependencies INTERFACE
    advapi32
    winhttp
    wininet
  )
elseif(APPLE)
  target_link_libraries(xm8_ra_dependencies INTERFACE
    "-framework Foundation"
    "-framework Security"
    "-framework SystemConfiguration"
  )
elseif(UNIX AND NOT ANDROID)
  find_package(CURL REQUIRED)
  find_package(Threads REQUIRED)
  target_link_libraries(xm8_ra_dependencies INTERFACE
    CURL::libcurl
    Threads::Threads
  )

  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBSECRET QUIET IMPORTED_TARGET libsecret-1)
  endif()
  if(LIBSECRET_FOUND)
    target_link_libraries(xm8_ra_dependencies INTERFACE PkgConfig::LIBSECRET)
    target_compile_definitions(xm8_ra_platform PRIVATE XM8_RA_HAS_LIBSECRET=1)
  else()
    message(STATUS
      "libsecret-1 not found; RetroAchievements credentials will not persist")
  endif()
endif()

function(xm8_enable_ra_for_target target_name)
  target_sources(${target_name} PRIVATE $<TARGET_OBJECTS:xm8_ra_core>)
  target_sources(${target_name} PRIVATE $<TARGET_OBJECTS:xm8_ra_platform>)
  target_sources(${target_name} PRIVATE $<TARGET_OBJECTS:xm8_ra_media_xm8>)
  target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/Source/RA)
  target_link_libraries(${target_name} PRIVATE xm8_ra_dependencies)
  target_compile_definitions(${target_name} PRIVATE XM8_ENABLE_RETROACHIEVEMENTS=1)
endfunction()
