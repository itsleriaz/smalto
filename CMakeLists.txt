cmake_minimum_required(VERSION 3.24)

project(Smalto VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SMALTO_BUILD_APP "Build the Qt application" ON)
option(SMALTO_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(core)
add_subdirectory(formats)

if(SMALTO_BUILD_APP)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Network Quick ShaderTools)
    if(SMALTO_BUILD_TESTS)
        find_package(Qt6 REQUIRED COMPONENTS Test)
    endif()
    # Qt >= 6.9 ships private module targets as separate packages.
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 QUIET COMPONENTS GuiPrivate)

    qt_standard_project_setup(REQUIRES 6.8)

    qt_add_executable(smalto WIN32 MACOSX_BUNDLE
        app/main.cpp
    )
    if(APPLE)
        enable_language(OBJCXX)
        target_sources(smalto PRIVATE app/MacTitleBar.mm)
        target_link_libraries(smalto PRIVATE "-framework AppKit")
    endif()

    set(SMALTO_QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/CurveEditor.qml
        qml/Panel.qml
        qml/StatusBar.qml
        qml/AppMenus.qml
        qml/ModelMenu.qml
        qml/NativeMenuBar.qml
        qml/NativeItem.qml
        qml/AboutDialog.qml
        qml/FrameDurationDialog.qml
        qml/PanelColumns.qml
        qml/PanelTitle.qml
        qml/EdgeHandle.qml
        qml/ColorField.qml
        qml/GridHandles.qml
        qml/FloatingDialog.qml
        qml/LayersPanel.qml
        qml/TagsPanel.qml
        qml/LivePanel.qml
        qml/PickSwatch.qml
        qml/LayerMenu.qml
        qml/MenuButton.qml
        qml/MenuBarRow.qml
        qml/IconButton.qml
        qml/TopBar.qml
        qml/TitleBar.qml
        qml/WindowEdges.qml
        qml/ToolStrip.qml
        qml/ColorPanel.qml
        qml/ColorPicker.qml
        qml/Timeline.qml
        qml/Segmented.qml
        qml/OptionPill.qml
        qml/SegmentGroup.qml
        qml/SegmentButton.qml
        qml/ExportPopup.qml
        qml/GridPopup.qml
        qml/PreferencesDialog.qml
        qml/PreviewPanel.qml
        qml/EffectDialog.qml
        qml/BrushLibraryPopup.qml
        qml/ShortcutsSettings.qml
        qml/AboutSettings.qml
        qml/ToolOptions.qml
        qml/TagEditor.qml
        qml/NewSpriteDialog.qml
        qml/SpriteSizeDialog.qml
        qml/SpriteSheetDialog.qml
        qml/SpriteSheetHandles.qml
        qml/ImageFolderDialog.qml
        qml/ColorModeDialog.qml
        qml/CanvasSizeHandles.qml
        qml/CanvasSizePanel.qml
        qml/PalettePopup.qml
        qml/PaletteToolsPopup.qml
        qml/LospecBrowser.qml
        qml/DitherPopup.qml
        qml/PenPopup.qml
        qml/StabilizerOverlay.qml
        qml/SymmetryPopup.qml
        qml/SymmetryHandles.qml
        qml/LiveEmitterHandle.qml
        qml/SmearHandles.qml
        qml/SmearPanel.qml
        qml/EasingCurveEditor.qml
        qml/LassoOverlay.qml
        qml/GradientOverlay.qml
        qml/GradientStopsBar.qml
        qml/SliceOverlay.qml
        qml/TilesetDialog.qml
        qml/TilesetImportDialog.qml
        qml/TilesetExportDialog.qml
    qml/TilesetPanel.qml
        qml/DocumentTabs.qml
        qml/Tips.qml
        qml/AppToolTip.qml
        qml/AppMenu.qml
        qml/AppMenuItem.qml
        qml/AppMenuSeparator.qml
        qml/AppTextField.qml
        qml/AppTextArea.qml
        qml/AppButton.qml
        qml/AppScrollBar.qml
        qml/TransformHandles.qml
        qml/NumberControl.qml
        qml/AppDialog.qml
        qml/AppComboBox.qml
    )
    # Flatten qml/ into the module root so files see each other by type name.
    foreach(file IN LISTS SMALTO_QML_FILES)
        get_filename_component(name ${file} NAME)
        set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
    endforeach()
    set_source_files_properties(qml/Theme.qml qml/Tips.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

    qt_add_qml_module(smalto
        URI Smalto
        VERSION 1.0
        QML_FILES ${SMALTO_QML_FILES}
        SOURCES
            app/Editor.h app/Editor.cpp
            app/CanvasView.h app/CanvasView.cpp
            app/FileOpenQueue.h app/FileOpenQueue.cpp
            app/RecentFiles.h app/RecentFiles.cpp
            app/MenuStyle.h app/MenuStyle.cpp
            app/Manual.h app/Manual.cpp
            app/PaletteLibrary.h app/PaletteLibrary.cpp
            app/LospecClient.h app/LospecClient.cpp
            app/TimelineModel.h app/TimelineModel.cpp
            app/TimelineGrid.h app/TimelineGrid.cpp
            app/ImagePreview.h app/ImagePreview.cpp
            app/Recovery.h app/Recovery.cpp
            app/ClickAway.h app/ClickAway.cpp
            app/Migration.h app/Migration.cpp
            app/BrushLibrary.h app/BrushLibrary.cpp
            app/Shortcuts.h app/Shortcuts.cpp
    )

    # qmllint on every build: a missing id or property fails it (any warning
    # does, see .qmllint.ini). Turn off if a newer Qt's qmllint flags
    # something new before it's fixed: -DSMALTO_LINT_QML=OFF.
    option(SMALTO_LINT_QML "Lint the QML with every build" ON)
    if(SMALTO_LINT_QML)
        # Part of the default build, after the app (the lint needs its types).
        add_custom_target(smalto_lint ALL)
        add_dependencies(smalto_lint smalto_qmllint)
    endif()

    qt_add_shaders(smalto "shaders"
        PREFIX "/"
        FILES
            shaders/canvas.vert
            shaders/canvas.frag
    )

    file(GLOB SMALTO_PALETTES CONFIGURE_DEPENDS resources/palettes/*.gpl)
    qt_add_resources(smalto "palettes"
        PREFIX "/palettes"
        BASE resources/palettes
        FILES ${SMALTO_PALETTES}
    )

    file(GLOB SMALTO_ICONS CONFIGURE_DEPENDS resources/icons/*.svg)
    qt_add_resources(smalto "icons"
        PREFIX "/icons"
        BASE resources/icons
        FILES ${SMALTO_ICONS}
    )
    # The text tool's fonts (public domain, see resources/fonts/README.md).
    qt_add_resources(smalto "fonts"
        PREFIX "/fonts"
        BASE resources/fonts
        FILES resources/fonts/4x6.bdf resources/fonts/5x7.bdf resources/fonts/6x10.bdf
    )

    target_link_libraries(smalto PRIVATE
        smalto_formats
        Qt6::Gui
        Qt6::GuiPrivate
        Qt6::Network
        Qt6::Quick
    )

    # The generated QML type registration includes our headers by file name.
    target_include_directories(smalto PRIVATE app)
    target_compile_definitions(smalto PRIVATE SMALTO_VERSION="${PROJECT_VERSION}")
    # A build from the source tree (not a package, which sets tests OFF) finds
    # the manual in manual/book; see Manual::folder().
    if(SMALTO_BUILD_TESTS)
        target_compile_definitions(smalto PRIVATE SMALTO_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    endif()
    if(MSVC)
        target_compile_options(smalto PRIVATE /utf-8)
    endif()

    # App icon: the .icns goes into the bundle's Resources (Info.plist names it),
    # the .ico is compiled into the Windows executable.
    if(APPLE)
        set(SMALTO_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/Smalto.icns)
        target_sources(smalto PRIVATE ${SMALTO_ICNS})
        set_source_files_properties(${SMALTO_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    elseif(WIN32)
        target_sources(smalto PRIVATE resources/icon/smalto.rc)
    endif()

    set_target_properties(smalto PROPERTIES
        OUTPUT_NAME Smalto
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME Smalto
        MACOSX_BUNDLE_GUI_IDENTIFIER app.smalto.Smalto
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
endif()

if(SMALTO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
