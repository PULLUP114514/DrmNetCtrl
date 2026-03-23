set(APPLICATION_NAME IpcController)
cmake_minimum_required(VERSION 3.10)
project(IpcController)

# 1. 先做库
add_library(my_lib STATIC
  lib/drm_warpper.c
  lib/log.c
  lib/spsc_queue.c
  lib/RREFont/RREFont.cpp
  lib/keyinput.c
  lib/fbdraw.c
  lib/fbdrawttf.c
  lib/i2c.c
  lib/spi.c
  lib/uart.c
  lib/ipc_client.c
  lib/ipc_common.c
  lib/uuid.c
  lib/cJSON.c
)

target_include_directories(my_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/lib
)
# 2. 主程序
add_executable(IpcController
  src/main.c
)
# 3. 链接库
target_link_libraries(IpcController
  my_lib
)
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/dist CACHE PATH "Install prefix" FORCE)
install(TARGETS ${APPLICATION_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/${APPLICATION_NAME})
install(FILES ${CMAKE_CURRENT_LIST_DIR}/appconfig.json DESTINATION ${CMAKE_INSTALL_PREFIX}/${APPLICATION_NAME})
