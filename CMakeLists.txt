cmake_minimum_required(VERSION 3.24)
project(holos-tts-core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(HOLOS_FETCH_DEPS "Download & build C++ deps via FetchContent" ON)
option(HOLOS_WITH_ORT "Enable ONNX Runtime generator backend" ON)
option(HOLOS_WITH_CT2 "Enable CTranslate2 (verbalizer/stress neural models)" OFF)
option(HOLOS_WITH_SERVER "Build the Drogon HTTP/WS server" OFF)
option(HOLOS_BUILD_TESTS "Build unit/golden tests" ON)

# ONNX Runtime: prebuilt tarball dir (contains include/ + lib/). Set -DHOLOS_ORT_ROOT=...
set(HOLOS_ORT_ROOT "" CACHE PATH "Path to extracted onnxruntime prebuilt (include/, lib/)")

include(cmake/deps.cmake)

# --- core library -----------------------------------------------------------
add_library(holos_core
  src/core/text_prep.cpp
  src/core/stress_dictionary.cpp
  src/core/tokenizer.cpp
  src/core/ipa_uk.cpp
)
target_include_directories(holos_core PUBLIC src)
target_link_libraries(holos_core PUBLIC nlohmann_json::nlohmann_json pcre2-8 utf8proc)

find_package(marisa QUIET)
if(marisa_FOUND)
  target_link_libraries(holos_core PUBLIC marisa)
else()
  # FetchContent add_subdirectory експортує alias Marisa::marisa
  target_link_libraries(holos_core PUBLIC Marisa::marisa)
endif()

if(HOLOS_WITH_ORT)
  target_sources(holos_core PRIVATE src/core/phonemize.cpp src/core/synth.cpp src/core/voices.cpp src/core/audio.cpp src/core/accentor.cpp)
  target_compile_definitions(holos_core PUBLIC HOLOS_WITH_ORT=1)
  if(HOLOS_ORT_ROOT)
    target_include_directories(holos_core PUBLIC ${HOLOS_ORT_ROOT}/include ${HOLOS_ORT_ROOT}/include/onnxruntime/core/session)
    target_link_directories(holos_core PUBLIC ${HOLOS_ORT_ROOT}/lib)
  endif()
  target_link_libraries(holos_core PUBLIC onnxruntime)
endif()

# Rule-based verbalizer (digits/dates/time -> Ukrainian words); no external deps.
target_sources(holos_core PRIVATE src/core/verbalizer.cpp)

# --- CLI --------------------------------------------------------------------
add_executable(holos-cli src/cli/holos_cli.cpp)
target_link_libraries(holos-cli PRIVATE holos_core)

# --- server (M2) ------------------------------------------------------------
if(HOLOS_WITH_SERVER)
  add_subdirectory(server)
endif()

# --- tests ------------------------------------------------------------------
if(HOLOS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "holos-tts-core ${PROJECT_VERSION} | ORT=${HOLOS_WITH_ORT} CT2=${HOLOS_WITH_CT2} SERVER=${HOLOS_WITH_SERVER} TESTS=${HOLOS_BUILD_TESTS}")
