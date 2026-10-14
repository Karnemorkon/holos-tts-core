# M0: мінімальні тести. Golden-тести PCM (порівняння з Python) — M1.
if(TARGET holos_core)
  add_executable(test_stress_dictionary test_stress_dictionary.cpp)
  target_link_libraries(test_stress_dictionary PRIVATE holos_core)
  add_test(NAME stress_dictionary COMMAND test_stress_dictionary "${CMAKE_SOURCE_DIR}/../stress.trie")
endif()
