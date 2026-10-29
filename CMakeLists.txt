cmake_minimum_required(VERSION 3.16)
project(mpc-furnace LANGUAGES C CXX)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(FUR "${CMAKE_SOURCE_DIR}/furnace")

# -----------------------------------------------------------------------------
# 1. Static zlib (from furnace/extern/zlib)
# -----------------------------------------------------------------------------
set(ZLIB_DIR "${FUR}/extern/zlib")
set(Z_HAVE_UNISTD_H 1)
set(HAVE_STDARG_H 1)
configure_file("${ZLIB_DIR}/zconf.h.cmakein" "${CMAKE_CURRENT_BINARY_DIR}/zconf.h" @ONLY)

add_library(zstatic STATIC
  "${ZLIB_DIR}/adler32.c"
  "${ZLIB_DIR}/compress.c"
  "${ZLIB_DIR}/crc32.c"
  "${ZLIB_DIR}/deflate.c"
  "${ZLIB_DIR}/gzclose.c"
  "${ZLIB_DIR}/gzlib.c"
  "${ZLIB_DIR}/gzread.c"
  "${ZLIB_DIR}/gzwrite.c"
  "${ZLIB_DIR}/inflate.c"
  "${ZLIB_DIR}/infback.c"
  "${ZLIB_DIR}/inftrees.c"
  "${ZLIB_DIR}/inffast.c"
  "${ZLIB_DIR}/trees.c"
  "${ZLIB_DIR}/uncompr.c"
  "${ZLIB_DIR}/zutil.c"
)
target_include_directories(zstatic PUBLIC "${CMAKE_CURRENT_BINARY_DIR}" "${ZLIB_DIR}")
target_compile_definitions(zstatic PRIVATE _DEFAULT_SOURCE)

# -----------------------------------------------------------------------------
# 2. Static fmt (from furnace/extern/fmt)
# -----------------------------------------------------------------------------
set(FMT_DIR "${FUR}/extern/fmt")
add_library(fmt STATIC
  "${FMT_DIR}/src/format.cc"
  "${FMT_DIR}/src/os.cc"
)
target_include_directories(fmt PUBLIC "${FMT_DIR}/include")

# -----------------------------------------------------------------------------
# 3. Static klattsch formant speech synth (from furnace/extern/klattsch-cpp)
# -----------------------------------------------------------------------------
set(KLATTSCH_DIR "${FUR}/extern/klattsch-cpp")
add_library(klattsch STATIC
  "${KLATTSCH_DIR}/src/klattsch.cpp"
  "${KLATTSCH_DIR}/src/phoneme_banks.cpp"
  "${KLATTSCH_DIR}/src/banks_registry.cpp"
)
target_include_directories(klattsch PUBLIC "${KLATTSCH_DIR}/include")

# -----------------------------------------------------------------------------
# 4. Static Furnace Engine (DivEngine + all chip emulation cores)
# -----------------------------------------------------------------------------
set_property(
  SOURCE "${FUR}/extern/opl/opl3.c"
  APPEND PROPERTY COMPILE_DEFINITIONS
  OPL_COMPAT_OLD_EG=1
)

set(FUR_ENGINE_SOURCES
  "${FUR}/src/log.cpp"
  "${FUR}/src/baseutils.cpp"
  "${FUR}/src/fileutils.cpp"
  "${FUR}/src/timeutils.cpp"
  "${FUR}/src/utfutils.cpp"
  "${FUR}/src/fileutils/abstract.cpp"
  "${FUR}/src/fileutils/cfile.cpp"
  "${FUR}/src/pch.cpp"

  "${FUR}/src/audio/abstract.cpp"
  "${FUR}/src/audio/midi.cpp"
  "${FUR}/src/audio/pipe.cpp"

  "${FUR}/extern/itcompress/compression.c"

  "${FUR}/extern/SAASound/src/SAAAmp.cpp"
  "${FUR}/extern/SAASound/src/SAADevice.cpp"
  "${FUR}/extern/SAASound/src/SAAEnv.cpp"
  "${FUR}/extern/SAASound/src/SAAFreq.cpp"
  "${FUR}/extern/SAASound/src/SAAImpl.cpp"
  "${FUR}/extern/SAASound/src/SAANoise.cpp"
  "${FUR}/extern/SAASound/src/SAASndC.cpp"
  "${FUR}/extern/SAASound/src/SAASound.cpp"

  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/core/vox/vox.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/es550x/es550x.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/es550x/es550x_alu.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/es550x/es550x_filter.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/es550x/es5504.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/es550x/es5505.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/es550x/es5506.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/k005289/k005289.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/k007232/k007232.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/k053260/k053260.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/msm6295/msm6295.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/n163/n163.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/scc/scc.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/vrcvi/vrcvi.cpp"
  "${FUR}/extern/vgsound_emu-modified/vgsound_emu/src/x1_010/x1_010.cpp"

  "${FUR}/extern/adpcm/bs_codec.c"
  "${FUR}/extern/adpcm/oki_codec.c"
  "${FUR}/extern/adpcm/yma_codec.c"
  "${FUR}/extern/adpcm/ymb_codec.c"
  "${FUR}/extern/adpcm/ymz_codec.c"

  "${FUR}/extern/adpcm-xq-s/adpcm-lib.c"

  "${FUR}/extern/opn/ym3438.c"
  "${FUR}/extern/Nuked-PSG/ympsg.c"
  "${FUR}/extern/opm/opm.c"
  "${FUR}/extern/Nuked-OPLL/opll.c"
  "${FUR}/extern/opl/opl3.c"
  "${FUR}/extern/Nuked-OPL2-Lite/opl2.c"
  "${FUR}/extern/Nuked-CQM/cqm.c"
  "${FUR}/extern/YM2203-LLE/fmopn.c"
  "${FUR}/extern/YM3812-LLE/fmopl2.c"
  "${FUR}/extern/YMF262-LLE/fmopl3.c"
  "${FUR}/extern/YMF276-LLE/fmopn2.c"
  "${FUR}/extern/ESFMu/esfm.c"
  "${FUR}/extern/ESFMu/esfm_registers.c"
  "${FUR}/extern/emu2413/emu2413.c"
  "${FUR}/extern/YM2608-LLE/fmopna_2608.c"
  "${FUR}/extern/YM2608-LLE/fmopna_2610.c"
  "${FUR}/extern/YM2608-LLE/fmopna_2612.c"
  "${FUR}/extern/YM2151-LLE/fmopm.c"
  "${FUR}/extern/YM2414-LLE/opz_lle.c"

  "${FUR}/extern/pwrnoise/pwrnoise.c"
  "${FUR}/extern/blip_buf/blip_buf.c"

  "${FUR}/src/engine/platform/sound/sn76496.cpp"
  "${FUR}/src/engine/platform/sound/ay8910.cpp"
  "${FUR}/src/engine/platform/sound/saa1099.cpp"
  "${FUR}/src/engine/platform/sound/namco.cpp"
  "${FUR}/src/engine/platform/sound/segapcm.cpp"
  "${FUR}/src/engine/platform/sound/gb/apu.c"
  "${FUR}/src/engine/platform/sound/gb/timing.c"
  "${FUR}/src/engine/platform/sound/pce_psg.cpp"
  "${FUR}/src/engine/platform/sound/nes/apu.cpp"
  "${FUR}/src/engine/platform/sound/nes/fds.cpp"
  "${FUR}/src/engine/platform/sound/nes/mmc5.cpp"
  "${FUR}/src/engine/platform/sound/vera_psg.c"
  "${FUR}/src/engine/platform/sound/vera_pcm.c"
  "${FUR}/src/engine/platform/sound/ymf278b/ymf278.cpp"
  "${FUR}/src/engine/platform/sound/atomicssg/ssg.c"

  "${FUR}/src/engine/platform/sound/nes_nsfplay/5e01_apu.cpp"
  "${FUR}/src/engine/platform/sound/nes_nsfplay/5e01_dmc.cpp"
  "${FUR}/src/engine/platform/sound/nes_nsfplay/nes_apu.cpp"
  "${FUR}/src/engine/platform/sound/nes_nsfplay/nes_dmc.cpp"
  "${FUR}/src/engine/platform/sound/nes_nsfplay/nes_fds.cpp"
  "${FUR}/src/engine/platform/sound/nes_nsfplay/nes_mmc5.cpp"
  "${FUR}/src/engine/platform/sound/nes_nsfplay/nes_n106.cpp"
  "${FUR}/src/engine/platform/sound/nes_nsfplay/nes_vrc6.cpp"

  "${FUR}/src/engine/platform/sound/c64/sid.cc"
  "${FUR}/src/engine/platform/sound/c64/voice.cc"
  "${FUR}/src/engine/platform/sound/c64/wave.cc"
  "${FUR}/src/engine/platform/sound/c64/envelope.cc"
  "${FUR}/src/engine/platform/sound/c64/filter.cc"
  "${FUR}/src/engine/platform/sound/c64/extfilt.cc"
  "${FUR}/src/engine/platform/sound/c64/pot.cc"
  "${FUR}/src/engine/platform/sound/c64/version.cc"
  "${FUR}/src/engine/platform/sound/c64/wave6581_PS_.cc"
  "${FUR}/src/engine/platform/sound/c64/wave6581_PST.cc"
  "${FUR}/src/engine/platform/sound/c64/wave6581_P_T.cc"
  "${FUR}/src/engine/platform/sound/c64/wave6581__ST.cc"
  "${FUR}/src/engine/platform/sound/c64/wave8580_PS_.cc"
  "${FUR}/src/engine/platform/sound/c64/wave8580_PST.cc"
  "${FUR}/src/engine/platform/sound/c64/wave8580_P_T.cc"
  "${FUR}/src/engine/platform/sound/c64/wave8580__ST.cc"

  "${FUR}/src/engine/platform/sound/c64_fp/array.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/Dac.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/EnvelopeGenerator.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/ExternalFilter.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/Filter6581.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/Filter8580.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/Filter.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/FilterModelConfig6581.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/FilterModelConfig8580.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/FilterModelConfig.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/Integrator6581.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/Integrator8580.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/OpAmp.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/SID.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/Spline.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/WaveformCalculator.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/WaveformGenerator.cpp"
  "${FUR}/src/engine/platform/sound/c64_fp/resample/SincResampler.cpp"

  "${FUR}/src/engine/platform/sound/c64_d/dsid.c"

  "${FUR}/src/engine/platform/sound/tia/AudioChannel.cpp"
  "${FUR}/src/engine/platform/sound/tia/Audio.cpp"

  "${FUR}/src/engine/platform/sound/ymfm/ymfm_adpcm.cpp"
  "${FUR}/src/engine/platform/sound/ymfm/ymfm_opl.cpp"
  "${FUR}/src/engine/platform/sound/ymfm/ymfm_opm.cpp"
  "${FUR}/src/engine/platform/sound/ymfm/ymfm_opn.cpp"
  "${FUR}/src/engine/platform/sound/ymfm/ymfm_opz.cpp"
  "${FUR}/src/engine/platform/sound/ymfm/ymfm_pcm.cpp"
  "${FUR}/src/engine/platform/sound/ymfm/ymfm_ssg.cpp"

  "${FUR}/src/engine/platform/sound/lynx/Mikey.cpp"
  "${FUR}/src/engine/platform/sound/pokey/mzpokeysnd.c"
  "${FUR}/src/engine/platform/sound/pokey/AltASAP.cpp"
  "${FUR}/src/engine/platform/sound/qsound.c"
  "${FUR}/src/engine/platform/sound/swan.c"
  "${FUR}/src/engine/platform/sound/su.cpp"
  "${FUR}/src/engine/platform/sound/vic20sound.c"
  "${FUR}/src/engine/platform/sound/ymz280b.cpp"
  "${FUR}/src/engine/platform/sound/vsu.cpp"
  "${FUR}/src/engine/platform/sound/t6w28/T6W28_Apu.cpp"
  "${FUR}/src/engine/platform/sound/rf5c68.cpp"
  "${FUR}/src/engine/platform/sound/oki/msm5232.cpp"
  "${FUR}/src/engine/platform/sound/oki/okim6258.cpp"
  "${FUR}/src/engine/platform/sound/snes/SPC_DSP.cpp"
  "${FUR}/src/engine/platform/sound/ga20/iremga20.cpp"
  "${FUR}/src/engine/platform/sound/sm8521.c"
  "${FUR}/src/engine/platform/sound/supervision.c"
  "${FUR}/src/engine/platform/sound/upd1771.cpp"
  "${FUR}/src/engine/platform/sound/d65modified.c"
  "${FUR}/src/engine/platform/sound/ted-sound.c"
  "${FUR}/src/engine/platform/sound/c140_c219.c"
  "${FUR}/src/engine/platform/sound/dave/dave.cpp"

  "${FUR}/src/engine/platform/sound/sid2/envelope.cc"
  "${FUR}/src/engine/platform/sound/sid2/filter.cc"
  "${FUR}/src/engine/platform/sound/sid2/sid.cc"
  "${FUR}/src/engine/platform/sound/sid2/version.cc"
  "${FUR}/src/engine/platform/sound/sid2/voice.cc"
  "${FUR}/src/engine/platform/sound/sid2/wave8580_PS_.cc"
  "${FUR}/src/engine/platform/sound/sid2/wave8580_PST.cc"
  "${FUR}/src/engine/platform/sound/sid2/wave8580_P_T.cc"
  "${FUR}/src/engine/platform/sound/sid2/wave8580__ST.cc"
  "${FUR}/src/engine/platform/sound/sid2/wave.cc"

  "${FUR}/src/engine/platform/sound/sid3.c"
  "${FUR}/src/engine/platform/sound/nds.cpp"

  "${FUR}/src/engine/platform/oplAInterface.cpp"
  "${FUR}/src/engine/platform/ym2608Interface.cpp"
  "${FUR}/src/engine/platform/ym2610Interface.cpp"

  "${FUR}/src/engine/fileOps/fileOpsCommon.cpp"
  "${FUR}/src/engine/fileOps/dmf.cpp"
  "${FUR}/src/engine/fileOps/fc.cpp"
  "${FUR}/src/engine/fileOps/ftm.cpp"
  "${FUR}/src/engine/fileOps/fur.cpp"
  "${FUR}/src/engine/fileOps/it.cpp"
  "${FUR}/src/engine/fileOps/midi.cpp"
  "${FUR}/src/engine/fileOps/mod.cpp"
  "${FUR}/src/engine/fileOps/s3m.cpp"
  "${FUR}/src/engine/fileOps/text.cpp"
  "${FUR}/src/engine/fileOps/tfm.cpp"
  "${FUR}/src/engine/fileOps/xm.cpp"
  "${FUR}/src/engine/fileOps/p.cpp"
  "${FUR}/src/engine/fileOps/p86.cpp"
  "${FUR}/src/engine/fileOps/pdx.cpp"
  "${FUR}/src/engine/fileOps/ppc.cpp"
  "${FUR}/src/engine/fileOps/pps.cpp"
  "${FUR}/src/engine/fileOps/pvi.cpp"
  "${FUR}/src/engine/fileOps/pzi.cpp"

  "${FUR}/src/engine/brrUtils.c"
  "${FUR}/src/engine/safeReader.cpp"
  "${FUR}/src/engine/safeWriter.cpp"
  "${FUR}/src/engine/workPool.cpp"
  "${FUR}/src/engine/assetDir.cpp"
  "${FUR}/src/engine/cmdStream.cpp"
  "${FUR}/src/engine/cmdStreamOps.cpp"
  "${FUR}/src/engine/config.cpp"
  "${FUR}/src/engine/configEngine.cpp"
  "${FUR}/src/engine/dispatchContainer.cpp"
  "${FUR}/src/engine/engine.cpp"
  "${FUR}/src/engine/export.cpp"
  "${FUR}/src/engine/exportDef.cpp"
  "${FUR}/src/engine/fileOpsIns.cpp"
  "${FUR}/src/engine/fileOpsSample.cpp"
  "${CMAKE_SOURCE_DIR}/src/filePlayer_stub.cpp"
  "${FUR}/src/engine/filter.cpp"
  "${FUR}/src/engine/instrument.cpp"
  "${FUR}/src/engine/legacySample.cpp"
  "${FUR}/src/engine/macroInt.cpp"
  "${FUR}/src/engine/pattern.cpp"
  "${FUR}/src/engine/pitchTable.cpp"
  "${FUR}/src/engine/playback.cpp"
  "${FUR}/src/engine/sample.cpp"
  "${FUR}/src/engine/song.cpp"
  "${FUR}/src/engine/sysDef.cpp"
  "${FUR}/src/engine/wavetable.cpp"
  "${FUR}/src/engine/waveSynth.cpp"
  "${FUR}/src/engine/wavOps.cpp"
  "${FUR}/src/engine/vgmOps.cpp"
  "${FUR}/src/engine/s98Ops.cpp"

  "${FUR}/src/engine/platform/abstract.cpp"
  "${FUR}/src/engine/platform/genesis.cpp"
  "${FUR}/src/engine/platform/genesisext.cpp"
  "${FUR}/src/engine/platform/sms.cpp"
  "${FUR}/src/engine/platform/opll.cpp"
  "${FUR}/src/engine/platform/gb.cpp"
  "${FUR}/src/engine/platform/pce.cpp"
  "${FUR}/src/engine/platform/mmc5.cpp"
  "${FUR}/src/engine/platform/nes.cpp"
  "${FUR}/src/engine/platform/c64.cpp"
  "${FUR}/src/engine/platform/arcade.cpp"
  "${FUR}/src/engine/platform/tx81z.cpp"
  "${FUR}/src/engine/platform/ym2203.cpp"
  "${FUR}/src/engine/platform/ym2203ext.cpp"
  "${FUR}/src/engine/platform/ym2608.cpp"
  "${FUR}/src/engine/platform/ym2608ext.cpp"
  "${FUR}/src/engine/platform/ym2610.cpp"
  "${FUR}/src/engine/platform/ym2610ext.cpp"
  "${FUR}/src/engine/platform/ym2610b.cpp"
  "${FUR}/src/engine/platform/ym2610bext.cpp"
  "${FUR}/src/engine/platform/ay.cpp"
  "${FUR}/src/engine/platform/ay8930.cpp"
  "${FUR}/src/engine/platform/opl.cpp"
  "${FUR}/src/engine/platform/fds.cpp"
  "${FUR}/src/engine/platform/tia.cpp"
  "${FUR}/src/engine/platform/saa.cpp"
  "${FUR}/src/engine/platform/amiga.cpp"
  "${FUR}/src/engine/platform/msm5232.cpp"
  "${FUR}/src/engine/platform/msm6258.cpp"
  "${FUR}/src/engine/platform/msm6295.cpp"
  "${FUR}/src/engine/platform/pcspkr.cpp"
  "${FUR}/src/engine/platform/segapcm.cpp"
  "${FUR}/src/engine/platform/qsound.cpp"
  "${FUR}/src/engine/platform/x1_010.cpp"
  "${FUR}/src/engine/platform/pokey.cpp"
  "${FUR}/src/engine/platform/lynx.cpp"
  "${FUR}/src/engine/platform/su.cpp"
  "${FUR}/src/engine/platform/swan.cpp"
  "${FUR}/src/engine/platform/t6w28.cpp"
  "${FUR}/src/engine/platform/vb.cpp"
  "${FUR}/src/engine/platform/vera.cpp"
  "${FUR}/src/engine/platform/zxbeeper.cpp"
  "${FUR}/src/engine/platform/zxbeeperquadtone.cpp"
  "${FUR}/src/engine/platform/bubsyswsg.cpp"
  "${FUR}/src/engine/platform/n163.cpp"
  "${FUR}/src/engine/platform/pet.cpp"
  "${FUR}/src/engine/platform/pokemini.cpp"
  "${FUR}/src/engine/platform/pong.cpp"
  "${FUR}/src/engine/platform/vic20.cpp"
  "${FUR}/src/engine/platform/vrc6.cpp"
  "${FUR}/src/engine/platform/es5506.cpp"
  "${FUR}/src/engine/platform/scc.cpp"
  "${FUR}/src/engine/platform/ymz280b.cpp"
  "${FUR}/src/engine/platform/namcowsg.cpp"
  "${FUR}/src/engine/platform/rf5c68.cpp"
  "${FUR}/src/engine/platform/snes.cpp"
  "${FUR}/src/engine/platform/k007232.cpp"
  "${FUR}/src/engine/platform/ga20.cpp"
  "${FUR}/src/engine/platform/sm8521.cpp"
  "${FUR}/src/engine/platform/supervision.cpp"
  "${FUR}/src/engine/platform/scvtone.cpp"
  "${FUR}/src/engine/platform/pv1000.cpp"
  "${FUR}/src/engine/platform/k053260.cpp"
  "${FUR}/src/engine/platform/ted.cpp"
  "${FUR}/src/engine/platform/c140.cpp"
  "${FUR}/src/engine/platform/esfm.cpp"
  "${FUR}/src/engine/platform/powernoise.cpp"
  "${FUR}/src/engine/platform/dave.cpp"
  "${FUR}/src/engine/platform/gbadma.cpp"
  "${FUR}/src/engine/platform/gbaminmod.cpp"
  "${FUR}/src/engine/platform/nds.cpp"
  "${FUR}/src/engine/platform/bifurcator.cpp"
  "${FUR}/src/engine/platform/klattsch.cpp"
  "${FUR}/src/engine/platform/sid2.cpp"
  "${FUR}/src/engine/platform/sid3.cpp"
  "${FUR}/src/engine/platform/multipcm.cpp"
  "${FUR}/src/engine/platform/pcmdac.cpp"
  "${FUR}/src/engine/platform/dummy.cpp"

  "${FUR}/src/engine/export/abstract.cpp"
  "${FUR}/src/engine/export/amigaValidation.cpp"
  "${FUR}/src/engine/export/sapr.cpp"
  "${FUR}/src/engine/export/tiuna.cpp"
  "${FUR}/src/engine/export/zsm.cpp"
  "${FUR}/src/engine/export/ipod.cpp"
  "${FUR}/src/engine/export/grub.cpp"

  "${FUR}/src/engine/effect/abstract.cpp"
  "${FUR}/src/engine/effect/dummy.cpp"
)

add_library(furnace_engine STATIC ${FUR_ENGINE_SOURCES})

target_include_directories(furnace_engine PUBLIC
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_SOURCE_DIR}/src"
  "${FUR}/src"
  "${FUR}/src/engine"
  "${FUR}/extern/IconFontCppHeaders"
  "${FUR}/extern/blip_buf"
  "${FUR}/src/icon"
  "${FUR}/extern/vgsound_emu-modified"
  "${FUR}/extern/fmt/include"
  "${FUR}/extern/zlib"
  "${FUR}/extern/klattsch-cpp/include"
)

target_compile_definitions(furnace_engine PUBLIC
  HAVE_DIRENT_TYPE
  _DEFAULT_SOURCE
)

target_compile_options(furnace_engine PRIVATE
  -Wno-unused-parameter
  -Wno-missing-field-initializers
  -Wno-sign-compare
  $<$<COMPILE_LANGUAGE:CXX>:-include${FUR}/src/pch.h>
  $<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_SOURCE_DIR}/src/sndfile_stub.h>
)

target_link_libraries(furnace_engine PUBLIC fmt klattsch zstatic)

add_custom_target(furnace-libs DEPENDS furnace_engine fmt klattsch zstatic)

file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/furnace-libs.txt" CONTENT
"$<TARGET_FILE:furnace_engine>
$<TARGET_FILE:fmt>
$<TARGET_FILE:klattsch>
$<TARGET_FILE:zstatic>
")
