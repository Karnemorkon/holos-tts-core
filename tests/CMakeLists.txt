# M0: мінімальні тести. Golden-тести PCM (порівняння з Python) — M1.
if(TARGET holos_core)
  add_executable(test_stress_dictionary test_stress_dictionary.cpp)
  target_link_libraries(test_stress_dictionary PRIVATE holos_core)
  add_test(NAME stress_dictionary COMMAND test_stress_dictionary "${CMAKE_SOURCE_DIR}/../stress.trie")

  # Golden parity for the ipa_uk phonemizer (vs Python ipa_uk.ipa).
  add_executable(test_ipa test_ipa.cpp)
  target_link_libraries(test_ipa PRIVATE holos_core)
  add_test(NAME ipa_golden COMMAND test_ipa "${CMAKE_CURRENT_SOURCE_DIR}/ipa_golden.tsv")

  # Golden parity for the text-prep stage (vs Python app.tts_pipeline text funcs).
  add_executable(test_text_prep test_text_prep.cpp)
  target_link_libraries(test_text_prep PRIVATE holos_core)
  add_test(NAME textprep_golden COMMAND test_text_prep "${CMAKE_CURRENT_SOURCE_DIR}/textprep_golden.json")

  # Golden parity for the CharTokenizer (vs Python CharTokenizer on phonemes).
  add_executable(test_tokenizer test_tokenizer.cpp)
  target_link_libraries(test_tokenizer PRIVATE holos_core)
  add_test(NAME tokenizer_golden COMMAND test_tokenizer
    "${CMAKE_CURRENT_SOURCE_DIR}/vocab.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tokenizer_golden.json")

  # M1e smoke: повний text->audio ланцюжок (потрібні моделі; не ctest).
  if(HOLOS_WITH_ORT)
    add_executable(test_synth test_synth.cpp)
    target_link_libraries(test_synth PRIVATE holos_core)
  endif()

  # Neural verbalizer golden (vs Python ONNX); skip (77) якщо моделі недоступні.
  if(HOLOS_WITH_VERBALIZER)
    add_executable(test_verbalizer_neural test_verbalizer_neural.cpp)
    target_link_libraries(test_verbalizer_neural PRIVATE holos_core)
    add_test(NAME verbalizer_neural_golden COMMAND test_verbalizer_neural
      "${CMAKE_CURRENT_SOURCE_DIR}/verbalizer_neural_golden.tsv"
      "${HOLOS_TEST_VERBALIZER_DIR}")
    set_tests_properties(verbalizer_neural_golden PROPERTIES SKIP_RETURN_CODE 77)

    # Gemma verbalizer: препроцесинг (без моделей) + golden (skip 77).
    add_executable(test_gemma_prep test_gemma_prep.cpp)
    target_link_libraries(test_gemma_prep PRIVATE holos_core)
    add_test(NAME gemma_prep COMMAND test_gemma_prep)

    add_executable(test_verbalizer_gemma test_verbalizer_gemma.cpp)
    target_link_libraries(test_verbalizer_gemma PRIVATE holos_core)
    add_test(NAME verbalizer_gemma_golden COMMAND test_verbalizer_gemma
      "${CMAKE_CURRENT_SOURCE_DIR}/gemma_verbalizer_golden.tsv"
      "${HOLOS_TEST_VERBALIZER_GEMMA_DIR}")
    set_tests_properties(verbalizer_gemma_golden PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()
