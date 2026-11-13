add_executable(glimmer_tests
  main.cpp
  test_memory.cpp
  test_gfx.cpp
  test_audio.cpp
  test_input.cpp
  test_cart.cpp
  test_runtime.cpp
  test_gif.cpp
  test_editors.cpp
)
target_compile_options(glimmer_tests PRIVATE ${GLIMMER_WARNINGS})
target_link_libraries(glimmer_tests PRIVATE glimmer_gfx glimmer_audio glimmer_input glimmer_runtime glimmer_editors glimmer_stb glimmer_doctest)
if(EMSCRIPTEN)
  # Tests read nothing from disk, so node can run them directly.
  target_link_options(glimmer_tests PRIVATE -sENVIRONMENT=node -sSTACK_SIZE=1048576)
  add_test(NAME glimmer_tests COMMAND node $<TARGET_FILE:glimmer_tests>)
else()
  add_test(NAME glimmer_tests COMMAND glimmer_tests)
endif()
