add_library(smalto_core STATIC
    Geometry.h
    Color.h
    Image.h Image.cpp
    Mask.h Mask.cpp
    Palette.h Palette.cpp
    BlendMode.h BlendMode.cpp
    Document.h Document.cpp
    Compositor.h Compositor.cpp
    Raster.h Raster.cpp
    Undo.h Undo.cpp
    Commands.h Commands.cpp
    Playback.h Playback.cpp
    Transform.h Transform.cpp
    ColorConvert.h ColorConvert.cpp
    PaletteTools.h PaletteTools.cpp
    Symmetry.h Symmetry.cpp
    Effects.h Effects.cpp
    Gradient.h Gradient.cpp
    Text.h Text.cpp
    Particles.h Particles.cpp
    Smear.h Smear.cpp
    Easing.h Easing.cpp
    Tilemap.h Tilemap.cpp
)

# Includes are written as "core/Image.h".
target_include_directories(smalto_core PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(smalto_core PUBLIC cxx_std_20)

# Live layers step their particles on several threads (core/Particles.cpp).
find_package(Threads REQUIRED)
target_link_libraries(smalto_core PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(smalto_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(smalto_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
