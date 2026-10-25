qt_add_library(nenia_midi_playback_backend STATIC
    NeniaMidiPlaybackEngine.cpp
    NeniaMidiPlaybackEngine.h
    NeniaMidiSequencer.cpp
    NeniaMidiSequencer.h
    NeniaStreamingPeakProtector.cpp
    NeniaStreamingPeakProtector.h
    NeniaPcmRingBuffer.h
    NeniaMidiPcmAnalysisTap.h
    ${PROJECT_SOURCE_DIR}/src/NeniaSpectrumPcmBlock.h
)

add_library(Nenia::MidiPlaybackBackend ALIAS nenia_midi_playback_backend)

target_include_directories(nenia_midi_playback_backend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(nenia_midi_playback_backend
    PUBLIC
        Qt6::Core
        Qt6::Multimedia
    PRIVATE
        Nenia::Nenia
        Nenia::FluidSynthPrototype
)

target_compile_features(nenia_midi_playback_backend PUBLIC cxx_std_20)
nenia_enable_warnings(nenia_midi_playback_backend)
