# Finite offline run on the built-in test track: glistening must exit 0 and write
# a complete binary PPM of the requested size. No audio device or terminal.
# cmake -DGLISTENING=<binary> -DOUT=<file.ppm> -P dump-smoke.cmake
file(REMOVE "${OUT}")
execute_process(COMMAND "${GLISTENING}" --test-signal --dump "${OUT}" --seconds 2 --size 320x180
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "glistening --dump exited with ${rc}")
endif()
file(SIZE "${OUT}" size)
file(READ "${OUT}" header LIMIT 15)
math(EXPR want "15 + 320 * 180 * 3")
if(NOT header STREQUAL "P6\n320 180\n255\n" OR NOT size EQUAL want)
  message(FATAL_ERROR "bad PPM: ${size} bytes (want ${want}), header '${header}'")
endif()
message(STATUS "DUMP SMOKE PASS")
