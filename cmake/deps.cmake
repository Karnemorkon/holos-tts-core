include(FetchContent)

# --- header-only / tiny deps (always) ---------------------------------------
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3)
FetchContent_MakeAvailable(nlohmann_json)

# marisa-trie: dictionary backend for Ukrainian word stress (CMake-native)
FetchContent_Declare(marisa
  GIT_REPOSITORY https://github.com/s-yata/marisa-trie.git
  GIT_TAG master)
set(MARISA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MARISA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(marisa)

# dr_wav: tiny public-domain WAV reader/writer (used when ORT audio path is on)
if(HOLOS_WITH_ORT)
  FetchContent_Declare(dr_wav
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG master)
  FetchContent_MakeAvailable(dr_wav)
  add_library(dr_wav INTERFACE)
  target_include_directories(dr_wav INTERFACE ${dr_wav_SOURCE_DIR})
endif()

# --- optional heavy deps (gated) --------------------------------------------
if(HOLOS_WITH_CT2)
  FetchContent_Declare(sentencepiece
    GIT_REPOSITORY https://github.com/google/sentencepiece.git
    GIT_TAG v0.2.2)
  set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(sentencepiece)

  FetchContent_Declare(ctranslate2
    GIT_REPOSITORY https://github.com/OpenNMT/CTranslate2.git
    GIT_TAG v4.8.3)
  set(WITH_MKL OFF CACHE BOOL "" FORCE)
  set(WITH_DNNL ON CACHE BOOL "" FORCE)
  set(BUILD_CLI OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(ctranslate2)
endif()

if(HOLOS_WITH_SERVER)
  FetchContent_Declare(drogon
    GIT_REPOSITORY https://github.com/drogonframework/drogon.git
    GIT_TAG v1.9.14)
  set(BUILD_CTL OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_ORM OFF CACHE BOOL "" FORCE)
  set(HAS_YAML_CONFIG OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(drogon)
endif()
