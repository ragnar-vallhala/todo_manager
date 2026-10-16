cmake_minimum_required(VERSION 3.16)
project(todo_manager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local")
find_package(Drogon CONFIG REQUIRED)
include(GNUInstallDirs)

# Web UI location baked into the server as its default --web dir. Points at
# the installed copy so the build tree can be deleted after `cmake --install`.
set(TODO_WEB_DIR "${CMAKE_INSTALL_FULL_DATADIR}/todo_manager/public")

# Server: REST API + static web UI
add_executable(todo_server
  server/main.cc
  server/TodoStore.cc
  server/TodoController.cc
  server/CategoryController.cc
  server/RoomController.cc
  server/RoomHub.cc
  server/McpController.cc
  server/ThemeController.cc)
target_include_directories(todo_server PRIVATE common)
target_link_libraries(todo_server PRIVATE Drogon::Drogon)
target_compile_definitions(todo_server PRIVATE
  TODO_PUBLIC_DIR="${TODO_WEB_DIR}")

# CLI client
add_executable(todo cli/main.cc cli/Tui.cc)
target_include_directories(todo PRIVATE common)
target_link_libraries(todo PRIVATE Drogon::Drogon)

install(TARGETS todo_server todo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY public/ DESTINATION ${TODO_WEB_DIR})
