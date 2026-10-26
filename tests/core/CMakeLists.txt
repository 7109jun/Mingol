mingol_add_test(mingol_core_tests
  SOURCES
    test_result.cpp
    test_math.cpp
    test_memory.cpp
    test_signal.cpp
    test_log.cpp
    test_job_system.cpp
    test_fs.cpp
    test_hash.cpp
    test_uuid.cpp
    test_json.cpp
  LIBS Mingol::Core)
