mingol_add_test(mingol_platform_tests
  SOURCES test_input.cpp test_window.cpp test_glfw.cpp
  LIBS Mingol::Platform)

# Real-window tests need a display. Register an extra run under Xvfb when it is installed; without
# a display the plain run above reports the GLFW tests as skipped. -noreset: by default Xvfb resets
# when its last client disconnects and refuses connections for a moment, which made the
# create/destroy cycle test fail intermittently with "Failed to open display" (server artifact).
find_program(MINGOL_XVFB_RUN xvfb-run)
if(MINGOL_XVFB_RUN AND NOT WIN32)
  add_test(NAME mingol_platform_tests_xvfb
    COMMAND ${MINGOL_XVFB_RUN} -a -s "-screen 0 1280x1024x24 -noreset" $<TARGET_FILE:mingol_platform_tests> Glfw)
  set_tests_properties(mingol_platform_tests_xvfb PROPERTIES TIMEOUT 120)
endif()

# Window + Vulkan presentation. Real-window checks, so the same display rules apply.
find_package(X11 QUIET)
mingol_add_test(mingol_window_rhi_tests
  SOURCES test_window_rhi.cpp x11_probe.cpp
  LIBS Mingol::Platform Mingol::RHI)
if(X11_FOUND AND NOT WIN32)
  target_compile_definitions(mingol_window_rhi_tests PRIVATE MINGOL_TEST_HAVE_XLIB=1)
  target_include_directories(mingol_window_rhi_tests SYSTEM PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(mingol_window_rhi_tests PRIVATE ${X11_LIBRARIES})
endif()
if(MINGOL_XVFB_RUN AND NOT WIN32)
  add_test(NAME mingol_window_rhi_tests_xvfb
    COMMAND ${MINGOL_XVFB_RUN} -a -s "-screen 0 1280x1024x24 -noreset" $<TARGET_FILE:mingol_window_rhi_tests>)
  set_tests_properties(mingol_window_rhi_tests_xvfb PROPERTIES TIMEOUT 300)
endif()
