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
endif()
