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
find_package(CURL REQUIRED)

add_library(
  assistantd_core
  src/third_party/cJSON/cJSON.c
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
target_include_directories(assistantd_core PRIVATE src/third_party/cJSON)
target_link_libraries(assistantd_core PRIVATE CURL::libcurl)

add_executable(assistantd src/assistantd/main.c)
target_link_libraries(assistantd PRIVATE assistantd_core)

if(BUILD_TESTING)
  find_package(Threads REQUIRED)

  add_executable(test_ring_buffer_shape tests/c/test_ring_buffer_shape.c)
  target_link_libraries(test_ring_buffer_shape PRIVATE assistantd_core)
  add_test(NAME ring_buffer_shape COMMAND test_ring_buffer_shape)

  add_executable(test_ring_buffer_spsc tests/c/test_ring_buffer_spsc.c)
  target_link_libraries(test_ring_buffer_spsc PRIVATE assistantd_core Threads::Threads)
  add_test(NAME ring_buffer_spsc COMMAND test_ring_buffer_spsc)

  add_executable(test_config_shape tests/c/test_config_shape.c)
  target_link_libraries(test_config_shape PRIVATE assistantd_core)
  add_test(NAME config_shape COMMAND test_config_shape)

  add_executable(test_audio_capture_shape tests/c/test_audio_capture_shape.c)
  target_link_libraries(test_audio_capture_shape PRIVATE assistantd_core)
  add_test(NAME audio_capture_shape COMMAND test_audio_capture_shape)

  add_executable(test_supervisor_shape tests/c/test_supervisor_shape.c)
  target_link_libraries(test_supervisor_shape PRIVATE assistantd_core)
  add_test(NAME supervisor_shape COMMAND test_supervisor_shape)

  add_executable(test_llm_adapter_shape tests/c/test_llm_adapter_shape.c)
  target_link_libraries(test_llm_adapter_shape PRIVATE assistantd_core)
  add_test(NAME llm_adapter_shape COMMAND test_llm_adapter_shape)
endif()
