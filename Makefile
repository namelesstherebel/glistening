CXXFLAGS = -O2 -std=c++17 -Wall
# MilkDrop mode needs the projectM 3.x C++ API; off automatically when it is missing.
PROJECTM ?= $(shell [ "$$(uname -s)" = Linux ] && pkg-config --atleast-version=3 libprojectM 2>/dev/null && echo 1 || echo 0)
ifeq ($(PROJECTM),1)
PM_FLAGS = -DGLISTENING_PROJECTM=1 $(shell pkg-config --cflags --libs libprojectM)
endif

# Linux and macOS. Windows (and any OS) builds with CMake: see CMakeLists.txt.
ifeq ($(shell uname -s),Darwin)
GPU = platform/gpu_cgl.cpp
AUDIO = platform/audio_macos.mm
GL_LIBS = -framework OpenGL
AUDIO_LIBS = -framework CoreAudio -framework AudioToolbox -framework Foundation
# The capture permission prompt needs NSAudioCaptureUsageDescription in the binary.
APP_FLAGS = -Wl,-sectcreate,__TEXT,__info_plist,platform/Info.plist
else
GPU = platform/gpu_egl.cpp
GL_LIBS = $$(pkg-config --cflags --libs egl) -ldl
endif
PLATFORM = platform/posix.cpp $(GPU) $(AUDIO)

SHADERS = scenes/common.glsl $(sort $(wildcard scenes/*.frag scenes/*.vert)) $(wildcard shaders/*)
HEADERS = $(wildcard *.hpp) platform/platform.hpp tests/fixtures.hpp

# -I.: shaders.inc is included with <>, found on the include path only.
glistening: viz.cpp $(HEADERS) $(PLATFORM) shaders.inc
	$(CXX) $(CXXFLAGS) -I. -o $@ viz.cpp $(PLATFORM) $(PM_FLAGS) $(GL_LIBS) $(AUDIO_LIBS) $(APP_FLAGS)

# Shaders are embedded as raw strings so the binary runs from anywhere.
shaders.inc: $(SHADERS)
	@for f in $(SHADERS); do printf '{"%s", R"__TV__(' "$$f"; cat "$$f"; printf ')__TV__"},\n'; done > $@

tests/test: tests/test.cpp $(HEADERS) platform/posix.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< platform/posix.cpp

tests/render: tests/render.cpp $(HEADERS) $(GPU) shaders.inc
	$(CXX) $(CXXFLAGS) -I. -o $@ $< $(GPU) $(GL_LIBS)

# Unit gates, the headless render gate (software GL is fine; no GL fails), then
# the packaged binary's own checks and a finite offline run on the test track.
test: tests/test tests/render glistening
	./tests/test
	./tests/render
	./tests/render --scene clockwork --size 1000x240
	./glistening --version
	./glistening --self-test
	./glistening --test-signal --dump tests/smoke.ppm --seconds 2 --size 320x180
	@test "$$(head -c 15 tests/smoke.ppm)" = "$$(printf 'P6\n320 180\n255')" && test $$(wc -c < tests/smoke.ppm) -eq $$((15 + 320 * 180 * 3)) \
		&& rm -f tests/smoke.ppm && echo "DUMP SMOKE PASS"

# Contact sheets of every scene, in its signature palette, into docs/ (needs python3 + Pillow: python3 -m pip install -r tools/requirements.txt).
sheets: tests/render
	rm -rf docs/frames
	./tests/render --sheets docs/frames --palette first
	python3 tools/contact.py docs/frames docs

clean:
	rm -f glistening tests/test tests/render shaders.inc tests/smoke.ppm

.PHONY: test sheets clean
