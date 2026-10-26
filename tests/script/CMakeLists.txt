mingol_add_test(mingol_script_tests
  SOURCES test_lexer.cpp test_parser.cpp test_vm_basic.cpp test_vm_lang.cpp test_vm_exc.cpp test_vm_reflect.cpp
          test_plugin.cpp test_diag.cpp
  LIBS Mingol::Script)

# A plain-C plugin that implements natives through the C ABI (tests/script/plugin/test_plugin.c).
add_library(mingol_test_plugin MODULE plugin/test_plugin.c)
target_include_directories(mingol_test_plugin PRIVATE ${PROJECT_SOURCE_DIR}/engine/script/include)
set_target_properties(mingol_test_plugin PROPERTIES PREFIX "" C_VISIBILITY_PRESET default)
add_library(mingol_test_plugin_bad_abi MODULE plugin/bad_abi.c)
set_target_properties(mingol_test_plugin_bad_abi PROPERTIES PREFIX "")
target_include_directories(mingol_test_plugin_bad_abi PRIVATE ${PROJECT_SOURCE_DIR}/engine/script/include)
add_dependencies(mingol_script_tests mingol_test_plugin mingol_test_plugin_bad_abi)
target_compile_definitions(mingol_script_tests PRIVATE
  MINGOL_TEST_PLUGIN="$<TARGET_FILE:mingol_test_plugin>"
  MINGOL_TEST_PLUGIN_BAD="$<TARGET_FILE:mingol_test_plugin_bad_abi>")
