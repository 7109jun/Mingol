mingol_add_test(mingol_rhi_tests
  SOURCES test_rhi.cpp
  LIBS Mingol::RHI Mingol::Shader)

if(MINGOL_VALGRIND_TESTS AND NOT MSVC)
  find_program(MINGOL_VALGRIND valgrind)
  if(MINGOL_VALGRIND)
    add_test(NAME mingol_rhi_tests_valgrind
      COMMAND ${MINGOL_VALGRIND} --leak-check=full --show-leak-kinds=definite,indirect
              --keep-debuginfo=yes --error-exitcode=9 -q
              --suppressions=${PROJECT_SOURCE_DIR}/tests/valgrind.supp $<TARGET_FILE:mingol_rhi_tests>)
    set_tests_properties(mingol_rhi_tests_valgrind PROPERTIES TIMEOUT 900)
  else()
    message(WARNING "MINGOL_VALGRIND_TESTS=ON but valgrind was not found")
  endif()
endif()
