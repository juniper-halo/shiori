cmake_minimum_required(VERSION 3.20)

project(assistantd VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(CTest)

add_library(
  assistantd_core
  src/assistantd/status.c
  src/assistantd/logger.c
  src/assistantd/config.c
  src/assistantd/ring_buffer.c
  src/assistantd/audio_capture.c
  src/assistantd/vad_detector.c
  src/assistantd/stt_adapter.c
  src/assistantd/llm_adapter.c
  src/assistantd/tts_adapter.c
  src/assistantd/playback.c
  src/assistantd/shutdown.c
  src/assistantd/supervisor.c
)

target_include_directories(assistantd_core PUBLIC include)

add_executable(assistantd src/assistantd/main.c)
target_link_libraries(assistantd PRIVATE assistantd_core)

if(BUILD_TESTING)
  add_executable(test_ring_buffer_shape tests/c/test_ring_buffer_shape.c)
  target_link_libraries(test_ring_buffer_shape PRIVATE assistantd_core)
  add_test(NAME ring_buffer_shape COMMAND test_ring_buffer_shape)

  add_executable(test_config_shape tests/c/test_config_shape.c)
  target_link_libraries(test_config_shape PRIVATE assistantd_core)
  add_test(NAME config_shape COMMAND test_config_shape)

  add_executable(test_supervisor_shape tests/c/test_supervisor_shape.c)
  target_link_libraries(test_supervisor_shape PRIVATE assistantd_core)
  add_test(NAME supervisor_shape COMMAND test_supervisor_shape)
endif()
