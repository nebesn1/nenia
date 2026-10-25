qt_add_resources(NENIA_RESOURCE_SOURCES
    "${PROJECT_SOURCE_DIR}/src/NeniaResources.qrc"
)

qt_add_library(nenia_lib STATIC
    NeniaAnimationEngine.cpp
    NeniaAnimationEngine.h
    NeniaAnimationState.h
    NeniaColorScheme.cpp
    NeniaColorScheme.h
    NeniaKeyboardRange.h
    NeniaOctaveNaming.cpp
    NeniaOctaveNaming.h
    NeniaMidiChannelState.cpp
    NeniaMidiChannelState.h
    NeniaMidiEvent.cpp
    NeniaMidiEvent.h
    NeniaMidiSequence.cpp
    NeniaMidiSequence.h
    NeniaMidiVisualTimelineProvider.cpp
    NeniaMidiVisualTimelineProvider.h
    NeniaArtworkLoader.cpp
    NeniaArtworkLoader.h
    JsonTimelineProvider.cpp
    MidiTimelineProvider.cpp
    NeniaSpectrumAnalyzer.cpp
    NeniaSpectrumAnalyzer.h
    NeniaSpectrumFrame.h
    NeniaSpectrumPcmBlock.h
    NeniaSpectrumFrameStore.cpp
    NeniaSpectrumFrameStore.h
    NeniaPlaybackController.cpp
    NeniaAlternatePlaybackBackend.h
    NeniaDirectMidiSource.h
    NeniaMidiAudioTrace.h
    NeniaMediaMetadata.cpp
    NeniaMediaMetadata.h
    NeniaPerformanceDiagnostics.cpp
    NeniaPerformanceDiagnostics.h
    NeniaTagMetadataReader.cpp
    NeniaTagMetadataReader.h
    NeniaExtendedMediaInfo.h
    NeniaPlaylistController.cpp
    NeniaPlaylistFile.cpp
    NeniaTimelineSynchronizer.h
    NeniaTimelineSynchronizer.cpp
    NeniaWidget.cpp
    NeniaRenderer.cpp
    NeniaRenderer.h
    NeniaTypography.cpp
    NeniaTypography.h
    ${NENIA_RESOURCE_SOURCES}
    ${PROJECT_SOURCE_DIR}/include/nenia/JsonTimelineProvider.h
    ${PROJECT_SOURCE_DIR}/include/nenia/MidiTimelineProvider.h
    ${PROJECT_SOURCE_DIR}/include/nenia/Nenia.h
    ${PROJECT_SOURCE_DIR}/include/nenia/NeniaMediaInfo.h
    ${PROJECT_SOURCE_DIR}/include/nenia/NeniaPlaybackController.h
    ${PROJECT_SOURCE_DIR}/include/nenia/NeniaPlaylistController.h
    ${PROJECT_SOURCE_DIR}/include/nenia/NeniaPlaylistFile.h
    ${PROJECT_SOURCE_DIR}/include/nenia/NeniaUiState.h
    ${PROJECT_SOURCE_DIR}/include/nenia/TimelineEvent.h
    ${PROJECT_SOURCE_DIR}/include/nenia/TimelineEventProvider.h
    ${PROJECT_SOURCE_DIR}/include/nenia/NeniaWidget.h
)

add_library(Nenia::Nenia ALIAS nenia_lib)

set_target_properties(nenia_lib PROPERTIES
    EXPORT_NAME Nenia
)

target_include_directories(nenia_lib
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(nenia_lib
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Widgets
    PRIVATE
        TagLib::tag
)

target_compile_features(nenia_lib PUBLIC cxx_std_20)

if(BUILD_TESTING)
    target_compile_definitions(nenia_lib PRIVATE NENIA_TEST_HOOKS_ENABLED)
endif()

if(NENIA_BUILD_MIDI_PLAYBACK_BACKEND)
    target_compile_definitions(nenia_lib PRIVATE NENIA_ENABLE_DIRECT_MIDI)
endif()

nenia_enable_warnings(nenia_lib)

if(NENIA_BUILD_FLUIDSYNTH_PROTOTYPE)
    add_subdirectory(fluidsynth_prototype)
endif()

if(NENIA_BUILD_MIDI_PLAYBACK_BACKEND)
    add_subdirectory(midi_playback_backend)
endif()
