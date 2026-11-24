include(FetchContent)

# Header-only; SOURCE_SUBDIR points nowhere so doctest's own CMake isn't run.
# Targets define DOCTEST_CONFIG_USE_STD_HEADERS: with doctest's forward-declared
# std::ostream, MSVC 14.50 fails to stringify std::shared_ptr in CHECKs.
FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
    SOURCE_SUBDIR _none
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(doctest)

add_executable(smalto_tests
    main.cpp
    test_image.cpp
    test_blend.cpp
    test_document.cpp
    test_raster.cpp
    test_aseprite.cpp
    test_import.cpp
    test_playback.cpp
    test_transform.cpp
    test_palette.cpp
    test_palettetools.cpp
    test_symmetry.cpp
    test_colormode.cpp
    test_effects.cpp
    test_text.cpp
    test_particles.cpp
    test_livelayer.cpp
    test_smear.cpp
    test_pen.cpp
)
target_include_directories(smalto_tests PRIVATE ${doctest_SOURCE_DIR})
target_link_libraries(smalto_tests PRIVATE smalto_formats)
target_compile_definitions(smalto_tests PRIVATE SMALTO_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus" SMALTO_FONT_DIR="${PROJECT_SOURCE_DIR}/resources/fonts" DOCTEST_CONFIG_USE_STD_HEADERS)
if(MSVC)
    target_compile_options(smalto_tests PRIVATE /utf-8)
endif()

add_test(NAME smalto_tests COMMAND smalto_tests)

# Editor (Qt glue) tests, run headless on the offscreen platform.
if(SMALTO_BUILD_APP)
    add_executable(smalto_app_tests
        app/main.cpp
        app/test_editor.cpp
        app/test_export.cpp
        app/test_files.cpp
        app/test_tools.cpp
        app/test_timeline.cpp
        app/test_clipboard.cpp
        app/test_palettes.cpp
        app/test_documents.cpp
        app/test_recovery.cpp
        app/test_colormode.cpp
        app/test_lospec.cpp
        app/test_shortcuts.cpp
        app/test_brushes.cpp
        app/test_pen.cpp
        app/test_migration.cpp
        app/test_scrollclick.cpp
        app/test_previewzoom.cpp
        ${PROJECT_SOURCE_DIR}/app/Editor.h
        ${PROJECT_SOURCE_DIR}/app/Editor.cpp
        ${PROJECT_SOURCE_DIR}/app/FileOpenQueue.h
        ${PROJECT_SOURCE_DIR}/app/FileOpenQueue.cpp
        ${PROJECT_SOURCE_DIR}/app/RecentFiles.h
        ${PROJECT_SOURCE_DIR}/app/RecentFiles.cpp
        ${PROJECT_SOURCE_DIR}/app/Shortcuts.h
        ${PROJECT_SOURCE_DIR}/app/Shortcuts.cpp
        ${PROJECT_SOURCE_DIR}/app/BrushLibrary.h
        ${PROJECT_SOURCE_DIR}/app/BrushLibrary.cpp
        ${PROJECT_SOURCE_DIR}/app/PaletteLibrary.h
        ${PROJECT_SOURCE_DIR}/app/PaletteLibrary.cpp
        ${PROJECT_SOURCE_DIR}/app/LospecClient.h
        ${PROJECT_SOURCE_DIR}/app/LospecClient.cpp
        ${PROJECT_SOURCE_DIR}/app/Recovery.h
        ${PROJECT_SOURCE_DIR}/app/Recovery.cpp
        ${PROJECT_SOURCE_DIR}/app/TimelineModel.h
        ${PROJECT_SOURCE_DIR}/app/TimelineModel.cpp
        ${PROJECT_SOURCE_DIR}/app/Migration.h
        ${PROJECT_SOURCE_DIR}/app/Migration.cpp
        ${PROJECT_SOURCE_DIR}/app/ScrollClick.h
        ${PROJECT_SOURCE_DIR}/app/ScrollClick.cpp
        ${PROJECT_SOURCE_DIR}/app/ImagePreview.h
        ${PROJECT_SOURCE_DIR}/app/ImagePreview.cpp
    )
    set_target_properties(smalto_app_tests PROPERTIES AUTOMOC ON)
    target_include_directories(smalto_app_tests PRIVATE ${doctest_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/app)
    target_link_libraries(smalto_app_tests PRIVATE smalto_formats Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Test)
    target_compile_definitions(smalto_app_tests PRIVATE SMALTO_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus" DOCTEST_CONFIG_USE_STD_HEADERS)
    if(MSVC)
        target_compile_options(smalto_app_tests PRIVATE /utf-8)
    endif()
    # The built-in palette presets, as in the app.
    file(GLOB SMALTO_PALETTES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/palettes/*.gpl)
    qt_add_resources(smalto_app_tests "palettes"
        PREFIX "/palettes"
        BASE ${PROJECT_SOURCE_DIR}/resources/palettes
        FILES ${SMALTO_PALETTES}
    )
    qt_add_resources(smalto_app_tests "fonts"
        PREFIX "/fonts"
        BASE ${PROJECT_SOURCE_DIR}/resources/fonts
        FILES ${PROJECT_SOURCE_DIR}/resources/fonts/4x6.bdf ${PROJECT_SOURCE_DIR}/resources/fonts/5x7.bdf
              ${PROJECT_SOURCE_DIR}/resources/fonts/6x10.bdf
    )
    add_test(NAME smalto_app_tests COMMAND smalto_app_tests)
endif()
