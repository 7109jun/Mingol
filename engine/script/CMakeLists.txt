add_library(mingol_script STATIC
  src/lexer.cpp
  src/parser.cpp
  src/types.cpp
  src/symbols.cpp
  src/compiler_decl.cpp
  src/checker_core.cpp
  src/checker_expr.cpp
  src/checker_stmt.cpp
  src/codegen.cpp
  src/codegen_expr.cpp
  src/prelude.cpp
  src/compile.cpp
  src/disasm.cpp
  src/vm_heap.cpp
  src/vm_exec.cpp
  src/vm_api.cpp
  src/vm_natives.cpp
  src/plugin.cpp
)
add_library(Mingol::Script ALIAS mingol_script)
target_include_directories(mingol_script PUBLIC include PRIVATE src)
target_link_libraries(mingol_script PUBLIC Mingol::Core ${CMAKE_DL_LIBS})
mingol_target_options(mingol_script)
