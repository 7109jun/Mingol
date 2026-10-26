mingol_add_test(mingol_package_tests
  SOURCES
    test_mindev.cpp
    test_crypto.cpp
    test_archive.cpp
  LIBS Mingol::Package)
target_include_directories(mingol_package_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
