add_executable(glimmer_tests
  main.cpp
  test_memory.cpp
  test_gfx.cpp
  test_audio.cpp
  test_input.cpp
)
target_compile_options(glimmer_tests PRIVATE ${GLIMMER_WARNINGS})
target_link_libraries(glimmer_tests PRIVATE glimmer_gfx glimmer_audio glimmer_input glimmer_doctest)
target_compile_definitions(glimmer_tests PRIVATE GLIMMER_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME glimmer_tests COMMAND glimmer_tests)
