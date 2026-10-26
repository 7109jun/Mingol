mingol_add_test(mingol_renderer_cpu_tests
  SOURCES test_animation.cpp
  LIBS Mingol::Renderer)

mingol_add_test(mingol_renderer_gpu_tests
  SOURCES test_render.cpp
  LIBS Mingol::Renderer Mingol::RHI)
target_compile_definitions(mingol_renderer_gpu_tests PRIVATE MINGOL_TEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

if(MINGOL_VALGRIND_TESTS AND NOT MSVC)
  find_program(MINGOL_VALGRIND valgrind)
  if(MINGOL_VALGRIND)
    add_test(NAME mingol_renderer_gpu_tests_valgrind
      COMMAND ${MINGOL_VALGRIND} --leak-check=full --show-leak-kinds=definite,indirect
              --keep-debuginfo=yes --error-exitcode=9 -q
              --suppressions=${PROJECT_SOURCE_DIR}/tests/valgrind.supp $<TARGET_FILE:mingol_renderer_gpu_tests>)
    set_tests_properties(mingol_renderer_gpu_tests_valgrind PROPERTIES TIMEOUT 1800)
  endif()
endif()
