cmake_minimum_required(VERSION 3.16)

project(teraterm_ohos_entry LANGUAGES CXX)

add_subdirectory(
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../native
  ${CMAKE_BINARY_DIR}/teraterm_ohos_native
)
