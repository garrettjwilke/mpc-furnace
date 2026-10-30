# Makefile for mpc-furnace (Furnace Tracker port for the Akai MPC Sample)

BUILD   = build
BIN     = bin
HOST    = $(BUILD)/host
DEV     = $(BUILD)/device
FUR_HOST_BUILD = $(BUILD)/furnace-host
FUR_DEV_BUILD  = $(BUILD)/furnace-device

TOOLKIT = mpc-ui-toolkit/src
FUR     = furnace

INCLUDES = -I src \
           -I $(TOOLKIT) \
           -I $(FUR)/src \
           -I $(FUR)/src/engine \
           -I $(FUR)/extern/fmt/include \
           -I $(FUR)/extern/zlib \
           -I $(FUR)/extern/klattsch-cpp/include \
           -I $(FUR)/extern/blip_buf \
           -I $(FUR)/extern/IconFontCppHeaders \
           -I $(FUR)/src/icon \
           -I $(FUR)/extern/vgsound_emu-modified

DEFS = -D_DEFAULT_SOURCE -DHAVE_DIRENT_TYPE

HOSTCC  = cc
HOSTCXX = c++
HOST_CFLAGS   = -O2 -g -Wall -Wextra -std=c11 $(DEFS) $(INCLUDES) -I $(FUR_HOST_BUILD)
HOST_CXXFLAGS = -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c++14 $(DEFS) $(INCLUDES) -I $(FUR_HOST_BUILD)

ZIGCC  = zig cc -target aarch64-linux-musl
ZIGCXX = zig c++ -target aarch64-linux-musl
DEV_CFLAGS   = -O2 -Wall -Wextra -std=c11 $(DEFS) $(INCLUDES) -I $(FUR_DEV_BUILD)
DEV_CXXFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-deprecated-literal-operator -std=c++14 $(DEFS) $(INCLUDES) -I $(FUR_DEV_BUILD)

JOBS ?= 8

.PHONY: all furnace-host furnace-device test deploy clean

all: $(BIN)/mpc-furnace

# -----------------------------------------------------------------------------
# CMake static Furnace Engine builds (Host & Cross-Compiled Device)
# -----------------------------------------------------------------------------
$(FUR_HOST_BUILD)/furnace-libs.txt: CMakeLists.txt
	cmake -S . -B $(FUR_HOST_BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(FUR_HOST_BUILD) --target furnace-libs -j$(JOBS)

furnace-host: $(FUR_HOST_BUILD)/furnace-libs.txt
	cmake --build $(FUR_HOST_BUILD) --target furnace-libs -j$(JOBS)

$(FUR_DEV_BUILD)/furnace-libs.txt: CMakeLists.txt cmake/aarch64-linux-musl.cmake
	cmake -S . -B $(FUR_DEV_BUILD) \
		-DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/aarch64-linux-musl.cmake \
		-DCMAKE_BUILD_TYPE=Release
	cmake --build $(FUR_DEV_BUILD) --target furnace-libs -j$(JOBS)

furnace-device: $(FUR_DEV_BUILD)/furnace-libs.txt
	cmake --build $(FUR_DEV_BUILD) --target furnace-libs -j$(JOBS)

# -----------------------------------------------------------------------------
# Host Test Runner
# -----------------------------------------------------------------------------
HOST_TK_OBJS = $(HOST)/tk/ui.o \
               $(HOST)/tk/widgets.o \
               $(HOST)/tk/filebrowser.o \
               $(HOST)/tk/usb_sd.o \
               $(HOST)/tk/input.o

HOST_APP_OBJS = $(HOST)/src/furnace_bridge.o \
                $(HOST)/src/vgm2fur.o \
                $(HOST)/src/control_surface.o \
                $(HOST)/src/ui_views.o \
                $(HOST)/test/test_runner.o

$(HOST)/tk/%.o: $(TOOLKIT)/%.c | $(HOST)/tk
	$(HOSTCC) $(HOST_CFLAGS) -MMD -MP -c -o $@ $<

$(HOST)/src/%.o: src/%.cpp | $(HOST)/src $(FUR_HOST_BUILD)/furnace-libs.txt
	$(HOSTCXX) $(HOST_CXXFLAGS) -MMD -MP -c -o $@ $<

$(HOST)/test/%.o: test/%.cpp | $(HOST)/test $(FUR_HOST_BUILD)/furnace-libs.txt
	$(HOSTCXX) $(HOST_CXXFLAGS) -MMD -MP -c -o $@ $<

$(HOST)/test-runner: $(FUR_HOST_BUILD)/furnace-libs.txt $(HOST_TK_OBJS) $(HOST_APP_OBJS)
	$(HOSTCXX) $(HOST_CXXFLAGS) -o $@ $(HOST_TK_OBJS) $(HOST_APP_OBJS) \
		$$(cat $(FUR_HOST_BUILD)/furnace-libs.txt) -lpthread -lm

test: $(HOST)/test-runner
	./$(HOST)/test-runner

# -----------------------------------------------------------------------------
# Device Static Binary (aarch64-linux-musl)
# -----------------------------------------------------------------------------
DEV_TK_OBJS = $(DEV)/tk/app.o \
              $(DEV)/tk/screen.o \
              $(DEV)/tk/surface.o \
              $(DEV)/tk/ui.o \
              $(DEV)/tk/widgets.o \
              $(DEV)/tk/filebrowser.o \
              $(DEV)/tk/usb_sd.o \
              $(DEV)/tk/input.o

DEV_C_OBJS = $(DEV)/src/audio_engine.o

DEV_CXX_OBJS = $(DEV)/src/furnace_bridge.o \
               $(DEV)/src/vgm2fur.o \
               $(DEV)/src/control_surface.o \
               $(DEV)/src/ui_views.o \
               $(DEV)/src/main.o

$(DEV)/tk/%.o: $(TOOLKIT)/%.c | $(DEV)/tk
	$(ZIGCC) $(DEV_CFLAGS) -MMD -MP -c -o $@ $<

$(DEV)/src/%.o: src/%.c | $(DEV)/src
	$(ZIGCC) $(DEV_CFLAGS) -MMD -MP -c -o $@ $<

$(DEV)/src/%.o: src/%.cpp | $(DEV)/src $(FUR_DEV_BUILD)/furnace-libs.txt
	$(ZIGCXX) $(DEV_CXXFLAGS) -MMD -MP -c -o $@ $<

$(BIN)/mpc-furnace: $(FUR_DEV_BUILD)/furnace-libs.txt $(DEV_TK_OBJS) $(DEV_C_OBJS) $(DEV_CXX_OBJS) | $(BIN)
	$(ZIGCXX) -static -O2 -s -o $@ $(DEV_TK_OBJS) $(DEV_C_OBJS) $(DEV_CXX_OBJS) \
		$$(cat $(FUR_DEV_BUILD)/furnace-libs.txt) -lpthread -lm

# -----------------------------------------------------------------------------
# Directories, Deploy & Clean
# -----------------------------------------------------------------------------
$(HOST)/tk $(HOST)/src $(HOST)/test $(DEV)/tk $(DEV)/src $(BIN):
	mkdir -p $@

deploy: $(BIN)/mpc-furnace
	../mpc-boot-thingy/setup.sh --ssh --add $(BIN)/mpc-furnace --title "mpc-furnace"

clean:
	rm -rf $(BUILD) $(BIN)

-include $(wildcard $(HOST)/tk/*.d $(HOST)/src/*.d $(HOST)/test/*.d $(DEV)/tk/*.d $(DEV)/src/*.d)

