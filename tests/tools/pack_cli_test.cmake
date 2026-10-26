# SPDX-License-Identifier: GPL-2.0-only
# Drives mingol-pack through a full workflow and checks exit codes and results.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/proj/assets" "${WORK}/proj/scripts" "${WORK}/proj/scenes")
file(WRITE "${WORK}/proj/assets/a.txt" "alpha")
file(WRITE "${WORK}/proj/scripts/s.mins" "class S extends Script { }")
file(WRITE "${WORK}/proj/scenes/main.minseen" "{}")
file(WRITE "${WORK}/proj/demo.mindev" "{\n \"format\": \"mindev\",\n \"formatVersion\": 1,\n \"name\": \"Demo\",\n \"id\": \"00000000-0000-4000-8000-000000000000\",\n \"engineVersion\": \"0.1.0\",\n \"entryScene\": \"scenes/main.minseen\",\n \"scenes\": [\"scenes/main.minseen\"],\n \"assetDirs\": [\"assets\"],\n \"scriptDirs\": [\"scripts\"]\n}\n")

function(run expect_ok)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(LAST_OUT "${out}" PARENT_SCOPE)
  set(LAST_ERR "${err}" PARENT_SCOPE)
  if(expect_ok AND NOT rc EQUAL 0)
    message(FATAL_ERROR "expected success but got ${rc}: ${ARGN}\n${out}\n${err}")
  endif()
  if(NOT expect_ok AND rc EQUAL 0)
    message(FATAL_ERROR "expected failure but it succeeded: ${ARGN}\n${out}")
  endif()
endfunction()

run(TRUE "${PACK}" genkey)
string(STRIP "${LAST_OUT}" KEY)
string(LENGTH "${KEY}" KEYLEN)
if(NOT KEYLEN EQUAL 64)
  message(FATAL_ERROR "genkey printed '${KEY}'")
endif()
file(WRITE "${WORK}/key.txt" "${KEY}\n")
file(WRITE "${WORK}/otherkey.txt" "0000000000000000000000000000000000000000000000000000000000000001\n")

# plain archive
run(TRUE "${PACK}" pack "${WORK}/proj/demo.mindev" -o "${WORK}/plain.mingol")
run(TRUE "${PACK}" list "${WORK}/plain.mingol")
if(NOT LAST_OUT MATCHES "plain archive, 4 entries" OR NOT LAST_OUT MATCHES "assets/a.txt")
  message(FATAL_ERROR "unexpected list output: ${LAST_OUT}")
endif()
run(TRUE "${PACK}" verify "${WORK}/plain.mingol")

# encrypted archive: needs the right key
run(TRUE "${PACK}" pack "${WORK}/proj/demo.mindev" -o "${WORK}/enc.mingol" --key-file "${WORK}/key.txt")
run(FALSE "${PACK}" list "${WORK}/enc.mingol")
run(FALSE "${PACK}" verify "${WORK}/enc.mingol" --key-file "${WORK}/otherkey.txt")
run(TRUE "${PACK}" verify "${WORK}/enc.mingol" --key-file "${WORK}/key.txt")
set(ENV{MINGOL_TEST_KEY} "${KEY}")
run(TRUE "${PACK}" verify "${WORK}/enc.mingol" --key-env MINGOL_TEST_KEY)
run(FALSE "${PACK}" verify "${WORK}/enc.mingol" --key-env MINGOL_NO_SUCH_VAR)
run(FALSE "${PACK}" verify "${WORK}/enc.mingol" --key-file "${WORK}/key.txt" --key-env MINGOL_TEST_KEY)

# passphrase-derived key
set(ENV{MINGOL_TEST_PASS} "correct horse battery staple")
run(TRUE "${PACK}" pack "${WORK}/proj/demo.mindev" -o "${WORK}/pass.mingol" --passphrase-env MINGOL_TEST_PASS --salt demo)
run(TRUE "${PACK}" verify "${WORK}/pass.mingol" --passphrase-env MINGOL_TEST_PASS --salt demo)
run(FALSE "${PACK}" verify "${WORK}/pass.mingol" --passphrase-env MINGOL_TEST_PASS --salt other)

# extraction reproduces the files
run(TRUE "${PACK}" extract "${WORK}/enc.mingol" "${WORK}/out" --key-file "${WORK}/key.txt")
file(READ "${WORK}/out/assets/a.txt" extracted)
if(NOT extracted STREQUAL "alpha")
  message(FATAL_ERROR "extracted content differs: '${extracted}'")
endif()
if(NOT EXISTS "${WORK}/out/project.mindev" OR NOT EXISTS "${WORK}/out/scenes/main.minseen")
  message(FATAL_ERROR "extraction is incomplete")
endif()

# embedding into an executable (any file works; the tool itself is used) and reading it back
run(TRUE "${PACK}" embed "${PACK}" "${WORK}/enc.mingol" -o "${WORK}/packed-tool")
run(TRUE "${PACK}" list "${WORK}/packed-tool" --key-file "${WORK}/key.txt")
run(FALSE "${PACK}" embed "${WORK}/packed-tool" "${WORK}/enc.mingol" -o "${WORK}/twice")
run(TRUE "${WORK}/packed-tool" genkey)   # the packed executable still runs

# damaged archives are rejected
file(READ "${WORK}/plain.mingol" raw HEX)
string(SUBSTRING "${raw}" 0 200 head)
file(WRITE "${WORK}/cut.hex" "${head}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${WORK}/plain.mingol" "${WORK}/damaged.mingol")
file(APPEND "${WORK}/damaged.mingol" "X")
run(FALSE "${PACK}" verify "${WORK}/damaged.mingol")
run(FALSE "${PACK}" verify "${WORK}/does-not-exist.mingol")

# usage errors
run(FALSE "${PACK}")
run(FALSE "${PACK}" pack)
run(FALSE "${PACK}" frobnicate)
message(STATUS "mingol-pack CLI test passed")
