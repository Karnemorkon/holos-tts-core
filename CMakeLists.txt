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
option(HOLOS_WITH_VERBALIZER "Neural verbalizer (M2M100 ONNX int8 + SentencePiece)" ON)
option(HOLOS_WITH_SERVER "Build the Drogon HTTP/WS server" OFF)
option(HOLOS_BUILD_TESTS "Build unit/golden tests" ON)

# ONNX Runtime: prebuilt tarball dir (contains include/ + lib/). Set -DHOLOS_ORT_ROOT=...
set(HOLOS_ORT_ROOT "" CACHE PATH "Path to extracted onnxruntime prebuilt (include/, lib/)")

# Каталог моделей для golden-тесту neural verbalizer (порожній -> тест skip).
set(HOLOS_TEST_VERBALIZER_DIR "" CACHE PATH "onnx-int8 verbalizer models dir for golden test")

# Каталог моделей для golden-тесту Gemma-вербалізатора (порожній -> тест skip).
set(HOLOS_TEST_VERBALIZER_GEMMA_DIR "" CACHE PATH "gemma int8 verbalizer models dir for golden test")

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

# Neural verbalizer (M2M100 ONNX int8 + SentencePiece) — паритет якості з Python CT2.
if(HOLOS_WITH_VERBALIZER)
  if(NOT HOLOS_WITH_ORT)
    message(FATAL_ERROR "HOLOS_WITH_VERBALIZER requires HOLOS_WITH_ORT=ON")
  endif()
  find_path(SENTENCEPIECE_INCLUDE_DIR sentencepiece_processor.h
    HINTS /usr/include /usr/local/include)
  find_library(SENTENCEPIECE_LIBRARY sentencepiece
    HINTS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)
  if(NOT SENTENCEPIECE_INCLUDE_DIR OR NOT SENTENCEPIECE_LIBRARY)
    message(FATAL_ERROR "SentencePiece not found (apt install libsentencepiece-dev) "
      "or configure with -DHOLOS_WITH_VERBALIZER=OFF")
  endif()
  target_sources(holos_core PRIVATE src/core/verbalizer_neural.cpp
                                src/core/verbalizer_gemma.cpp)
  target_compile_definitions(holos_core PUBLIC HOLOS_WITH_VERBALIZER=1)
  target_include_directories(holos_core PUBLIC ${SENTENCEPIECE_INCLUDE_DIR})
  target_link_libraries(holos_core PUBLIC ${SENTENCEPIECE_LIBRARY})
endif()

# --- CLI --------------------------------------------------------------------
add_executable(holos-cli src/cli/holos_cli.cpp)
target_link_libraries(holos-cli PRIVATE holos_core)

# --- server (M2) ------------------------------------------------------------
if(HOLOS_WITH_SERVER)
  add_subdirectory(server)
endif()

# --- install (Docker runtime image) -----------------------------------------
include(GNUInstallDirs)
install(TARGETS holos-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(HOLOS_WITH_SERVER)
  install(TARGETS holos-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
# ONNX Runtime shared libs для runtime-образу (копіюються у Dockerfile з /opt/ort).

# --- tests ------------------------------------------------------------------
if(HOLOS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "holos-tts-core ${PROJECT_VERSION} | ORT=${HOLOS_WITH_ORT} CT2=${HOLOS_WITH_CT2} SERVER=${HOLOS_WITH_SERVER} TESTS=${HOLOS_BUILD_TESTS}")
