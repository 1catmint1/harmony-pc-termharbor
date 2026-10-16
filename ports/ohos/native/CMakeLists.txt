cmake_minimum_required(VERSION 3.16)

project(teraterm_ohos_native LANGUAGES CXX)

option(TT_OHOS_BUILD_NAPI "Build HarmonyOS Node-API bridge" ON)
option(TT_OHOS_BUILD_HOST_TESTS "Build host-side native tests" OFF)
option(TT_OHOS_ENABLE_LIBSSH2 "Enable SSH2 transport through libssh2" OFF)
set(TT_OHOS_LIBSSH2_ROOT "" CACHE PATH "Optional prebuilt libssh2 root for the current OHOS ABI")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TT_OHOS_SSH_SOURCE src/ssh_transport_unavailable.cpp)
if(TT_OHOS_ENABLE_LIBSSH2)
  if(NOT TT_OHOS_LIBSSH2_ROOT AND DEFINED ENV{TT_OHOS_LIBSSH2_ROOT})
    set(TT_OHOS_LIBSSH2_ROOT "$ENV{TT_OHOS_LIBSSH2_ROOT}")
  endif()
  if(NOT TT_OHOS_LIBSSH2_ROOT)
    if(CMAKE_OHOS_ARCH_ABI)
      set(TT_OHOS_LIBSSH2_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/libssh2/${CMAKE_OHOS_ARCH_ABI}")
    elseif(OHOS_ARCH)
      set(TT_OHOS_LIBSSH2_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/libssh2/${OHOS_ARCH}")
    endif()
  endif()

  set(TT_OHOS_LIBSSH2_EXTRA_LIBRARIES "")
  if(TT_OHOS_LIBSSH2_ROOT AND EXISTS "${TT_OHOS_LIBSSH2_ROOT}/include/libssh2.h")
    set(LIBSSH2_INCLUDE_DIR "${TT_OHOS_LIBSSH2_ROOT}/include")
    set(LIBSSH2_LIBRARY "${TT_OHOS_LIBSSH2_ROOT}/lib/libssh2.a")
    list(APPEND TT_OHOS_LIBSSH2_EXTRA_LIBRARIES
      "${TT_OHOS_LIBSSH2_ROOT}/lib/libmbedcrypto.a"
      "${TT_OHOS_LIBSSH2_ROOT}/lib/libmbedtls.a"
      "${TT_OHOS_LIBSSH2_ROOT}/lib/libmbedx509.a"
      "${TT_OHOS_LIBSSH2_ROOT}/lib/libeverest.a"
      "${TT_OHOS_LIBSSH2_ROOT}/lib/libp256m.a"
    )
    find_library(TT_OHOS_ZLIB z)
    if(TT_OHOS_ZLIB)
      list(APPEND TT_OHOS_LIBSSH2_EXTRA_LIBRARIES ${TT_OHOS_ZLIB})
    endif()
  else()
    find_path(LIBSSH2_INCLUDE_DIR libssh2.h)
    find_library(LIBSSH2_LIBRARY ssh2)
  endif()

  if(NOT LIBSSH2_INCLUDE_DIR OR NOT LIBSSH2_LIBRARY)
    message(FATAL_ERROR "libssh2 was not found. Set LIBSSH2_INCLUDE_DIR and LIBSSH2_LIBRARY, or disable TT_OHOS_ENABLE_LIBSSH2.")
  endif()
  set(TT_OHOS_SSH_SOURCE src/ssh_transport_libssh2.cpp)
endif()

add_library(teraterm_ohos_core STATIC
  src/known_hosts_store.cpp
  src/macro_runner.cpp
  src/posix_serial_transport.cpp
  src/posix_socket_connector.cpp
  src/posix_tcp_transport.cpp
  src/profile_importer.cpp
  src/session_transport.cpp
  ${TT_OHOS_SSH_SOURCE}
  src/telnet_filter.cpp
  src/terminal_session.cpp
  src/terminal_session_core.cpp
)

target_include_directories(teraterm_ohos_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(teraterm_ohos_core
  PUBLIC
    TT_OHOS_ENABLE_LIBSSH2=$<BOOL:${TT_OHOS_ENABLE_LIBSSH2}>
)

if(TT_OHOS_ENABLE_LIBSSH2)
  target_include_directories(teraterm_ohos_core
    PRIVATE
      ${LIBSSH2_INCLUDE_DIR}
  )
  target_link_libraries(teraterm_ohos_core
    PUBLIC
      ${LIBSSH2_LIBRARY}
      ${TT_OHOS_LIBSSH2_EXTRA_LIBRARIES}
  )
endif()

if(TT_OHOS_BUILD_NAPI)
  add_library(teraterm_ohos SHARED
    src/tt_ohos_napi.cpp
  )

  target_link_libraries(teraterm_ohos
    PRIVATE
      teraterm_ohos_core
  )

  find_library(ACE_NAPI_LIB ace_napi.z)
  if(NOT ACE_NAPI_LIB)
    message(FATAL_ERROR "ace_napi.z was not found. Build this target with the HarmonyOS native toolchain.")
  endif()

  target_link_libraries(teraterm_ohos
    PRIVATE
      ${ACE_NAPI_LIB}
  )
endif()

if(TT_OHOS_BUILD_HOST_TESTS)
  find_package(Threads REQUIRED)

  add_executable(teraterm_stream_utf8_test tests/test_stream_utf8.cpp)
  target_link_libraries(teraterm_stream_utf8_test PRIVATE teraterm_ohos_core)

  add_executable(teraterm_ohos_core_test
    tests/test_terminal_session_core.cpp
  )

  target_link_libraries(teraterm_ohos_core_test
    PRIVATE
      teraterm_ohos_core
  )

  add_executable(teraterm_ohos_transport_test
    tests/test_terminal_session_transport.cpp
  )

  target_link_libraries(teraterm_ohos_transport_test
    PRIVATE
      teraterm_ohos_core
      Threads::Threads
  )

  add_executable(teraterm_ohos_profile_test
    tests/test_profile_importer.cpp
  )

  target_link_libraries(teraterm_ohos_profile_test
    PRIVATE
      teraterm_ohos_core
  )

  add_executable(teraterm_ohos_known_hosts_test
    tests/test_known_hosts_store.cpp
  )

  target_link_libraries(teraterm_ohos_known_hosts_test
    PRIVATE
      teraterm_ohos_core
  )

  add_executable(teraterm_ohos_ssh_config_test
    tests/test_ssh_transport_config.cpp
  )

  target_link_libraries(teraterm_ohos_ssh_config_test
    PRIVATE
      teraterm_ohos_core
  )

  add_executable(teraterm_ohos_ssh_file_transfer_smoke
    tests/test_ssh_file_transfer_smoke.cpp
  )

  target_link_libraries(teraterm_ohos_ssh_file_transfer_smoke
    PRIVATE
      teraterm_ohos_core
  )

  add_executable(teraterm_ohos_macro_test
    tests/test_macro_runner.cpp
  )

  target_link_libraries(teraterm_ohos_macro_test
    PRIVATE
      teraterm_ohos_core
      Threads::Threads
  )
endif()
