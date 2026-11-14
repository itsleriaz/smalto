include(FetchContent)

# zlib for Aseprite cel compression. Only the sources we need are compiled,
# so zlib's own CMake (install rules, examples, shared lib) isn't run.
FetchContent_Declare(zlib
    URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
    URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    SOURCE_SUBDIR _none
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(zlib)

add_library(smalto_zlib STATIC
    ${zlib_SOURCE_DIR}/adler32.c
    ${zlib_SOURCE_DIR}/compress.c
    ${zlib_SOURCE_DIR}/crc32.c
    ${zlib_SOURCE_DIR}/deflate.c
    ${zlib_SOURCE_DIR}/inffast.c
    ${zlib_SOURCE_DIR}/inflate.c
    ${zlib_SOURCE_DIR}/inftrees.c
    ${zlib_SOURCE_DIR}/trees.c
    ${zlib_SOURCE_DIR}/uncompr.c
    ${zlib_SOURCE_DIR}/zutil.c
)
target_include_directories(smalto_zlib SYSTEM PUBLIC ${zlib_SOURCE_DIR})
if(MSVC)
    target_compile_definitions(smalto_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
endif()

# stb_image (public domain) for PNG, JPEG, BMP, TGA and GIF decoding. A single header, pinned by commit.
FetchContent_Declare(stb_image
    URL https://raw.githubusercontent.com/nothings/stb/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h
    URL_HASH SHA256=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
    DOWNLOAD_NO_EXTRACT TRUE
    SOURCE_SUBDIR _none
)
FetchContent_MakeAvailable(stb_image)

add_library(smalto_stb STATIC third_party/stb_image_impl.c)
target_include_directories(smalto_stb SYSTEM PUBLIC ${stb_image_SOURCE_DIR})

add_library(smalto_formats STATIC
    FileIO.h FileIO.cpp
    Aseprite.h Aseprite.cpp
    LiveLayerData.h LiveLayerData.cpp
    Export.h Export.cpp
    TilesetExport.h TilesetExport.cpp
    ImageImport.h ImageImport.cpp
    PaletteIO.h PaletteIO.cpp
    Bdf.h Bdf.cpp
)
target_link_libraries(smalto_formats PUBLIC smalto_core PRIVATE smalto_zlib smalto_stb)
target_compile_definitions(smalto_formats PRIVATE SMALTO_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(smalto_formats PRIVATE /W4 /utf-8)
else()
    target_compile_options(smalto_formats PRIVATE -Wall -Wextra -Wpedantic)
endif()
