qt_add_executable(nenia_app
    main.cpp
    NeniaGlobalInput.cpp
    NeniaGlobalInput.h
    NeniaFileImport.cpp
    NeniaFileImport.h
    NeniaFileDialog.cpp
    NeniaFileDialog.h
    NeniaMenuBar.cpp
    NeniaMenuBar.h
    NeniaPlaybackOptionActions.cpp
    NeniaPlaybackOptionActions.h
    NeniaMidiSidecarResolver.cpp
    NeniaMidiSidecarResolver.h
    NeniaPlaylistActivity.cpp
    NeniaPlaylistActivity.h
    NeniaSettings.cpp
    NeniaSettings.h
)

set_target_properties(nenia_app PROPERTIES
    OUTPUT_NAME nenia
)

target_compile_definitions(nenia_app PRIVATE
    NENIA_PROJECT_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(nenia_app PRIVATE Nenia::Nenia)
target_include_directories(nenia_app PRIVATE ${PROJECT_SOURCE_DIR}/src)

if(TARGET Qt6::DBus)
    target_link_libraries(nenia_app PRIVATE Qt6::DBus)
    target_compile_definitions(nenia_app PRIVATE NENIA_HAS_XDG_PORTAL)
endif()

nenia_enable_warnings(nenia_app)

install(TARGETS nenia_app
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/nenia.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
endif()

if(NENIA_BUILD_MIDI_PLAYBACK_BACKEND)
    target_sources(nenia_app PRIVATE
        NeniaApplicationPlaybackRouter.cpp
        NeniaApplicationPlaybackRouter.h
        NeniaMidiMetadata.cpp
        NeniaMidiMetadata.h
    )
    target_compile_definitions(nenia_app PRIVATE NENIA_BUILD_MIDI_PLAYBACK_BACKEND)
    target_link_libraries(nenia_app PRIVATE Nenia::MidiPlaybackBackend)

endif()

if(BUILD_TESTING)
    add_custom_command(TARGET nenia_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:nenia_app>/demo"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/assets/demo/audio-playback-fixture.wav"
            "$<TARGET_FILE_DIR:nenia_app>/demo/audio-playback-fixture.wav"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/assets/demo/audio-timeline-fixture.json"
            "$<TARGET_FILE_DIR:nenia_app>/demo/audio-timeline-fixture.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/assets/demo/multichannel-midi-fixture.mid"
            "$<TARGET_FILE_DIR:nenia_app>/demo/multichannel-midi-fixture.mid"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/assets/demo/cover-artwork-fixture.png"
            "$<TARGET_FILE_DIR:nenia_app>/demo/cover-artwork-fixture.png"
    )
endif()
