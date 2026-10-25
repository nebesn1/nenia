qt_add_library(nenia_fluidsynth_prototype STATIC
    NeniaDiagnosticAudio.cpp
    NeniaDiagnosticAudio.h
    NeniaSoundFontSynthesizer.cpp
    NeniaSoundFontSynthesizer.h
    NeniaWavWriter.cpp
    NeniaWavWriter.h
)

add_library(Nenia::FluidSynthPrototype ALIAS nenia_fluidsynth_prototype)

target_include_directories(nenia_fluidsynth_prototype
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(nenia_fluidsynth_prototype
    PUBLIC
        Qt6::Core
    PRIVATE
        PkgConfig::FLUIDSYNTH
)

target_compile_features(nenia_fluidsynth_prototype PUBLIC cxx_std_20)
nenia_enable_warnings(nenia_fluidsynth_prototype)
