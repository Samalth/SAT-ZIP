cmake_minimum_required(VERSION 3.18)
project(satzip VERSION 0.6.0 LANGUAGES C)   # <-- ssot for version

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

# Generate satzip_version.h from the project version; everything (lib, CLI, GUI, .rc) uses it.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/satzip_version.h.in
               ${CMAKE_BINARY_DIR}/generated/satzip_version.h @ONLY)
if(WIN32)
    enable_language(RC)
    set(SATZIP_WIN_RC ${CMAKE_CURRENT_SOURCE_DIR}/assets/satzip.rc)
    if(MSVC)
        set(SATZIP_WIN_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/assets/satzip.manifest)
    endif()
endif()

if(MSVC)
    set(SATZIP_OPT /O2 /Oi /Gy)
    set(SATZIP_AVX2_OPT /arch:AVX2)
else()
    set(SATZIP_OPT -O3 -funroll-loops -fno-semantic-interposition -fomit-frame-pointer)
    set(SATZIP_AVX2_OPT -mavx2 -mbmi -mbmi2 -mfma)
endif()

set(SATZIP_HOT_SOURCES src/satzip_encode.c src/satzip_encode_r.c src/satzip_decode.c)
if(CMAKE_C_COMPILER_ARCHITECTURE_ID)
    string(TOLOWER "${CMAKE_C_COMPILER_ARCHITECTURE_ID}" _sz_cpu)
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _sz_cpu)
endif()
if(_sz_cpu MATCHES "^(amd64|x86_64|x64)$")
    set(SATZIP_DISPATCH_AVX2 ON)
endif()

add_library(satzip_hot_base OBJECT ${SATZIP_HOT_SOURCES})
target_compile_definitions(satzip_hot_base PRIVATE
    sz_compress_block=sz_compress_block_base
    sz_compress_block_r=sz_compress_block_r_base
    sz_decompress_block=sz_decompress_block_base)
target_compile_options(satzip_hot_base PRIVATE ${SATZIP_OPT})
set(SATZIP_HOT_OBJECTS $<TARGET_OBJECTS:satzip_hot_base>)
set(SATZIP_HOT_TARGETS satzip_hot_base)

if(SATZIP_DISPATCH_AVX2)
    add_library(satzip_hot_avx2 OBJECT ${SATZIP_HOT_SOURCES})
    target_compile_definitions(satzip_hot_avx2 PRIVATE
        sz_compress_block=sz_compress_block_avx2
        sz_compress_block_r=sz_compress_block_r_avx2
        sz_decompress_block=sz_decompress_block_avx2)
    target_compile_options(satzip_hot_avx2 PRIVATE ${SATZIP_OPT} ${SATZIP_AVX2_OPT})
    list(APPEND SATZIP_HOT_OBJECTS $<TARGET_OBJECTS:satzip_hot_avx2>)
    list(APPEND SATZIP_HOT_TARGETS satzip_hot_avx2)
endif()

foreach(_t ${SATZIP_HOT_TARGETS})
    target_include_directories(${_t} PRIVATE include ${CMAKE_BINARY_DIR}/generated)
endforeach()

add_library(satzip STATIC src/satzip_dispatch.c src/satzip_tans.c src/satzip_split.c src/satzip_stream.c
                          src/satzip_archive.c ${SATZIP_HOT_OBJECTS})
target_include_directories(satzip PUBLIC include ${CMAKE_BINARY_DIR}/generated)
if(SATZIP_DISPATCH_AVX2)
    target_compile_definitions(satzip PRIVATE SATZIP_DISPATCH_AVX2=1)
endif()

add_library(satzip_bench STATIC src/satzip_bench.c)
target_link_libraries(satzip_bench PUBLIC satzip)

target_compile_options(satzip       PRIVATE ${SATZIP_OPT})
target_compile_options(satzip_bench PRIVATE ${SATZIP_OPT})

add_executable(roundtrip test/roundtrip.c)
target_link_libraries(roundtrip PRIVATE satzip)

add_executable(profile test/profile.c)
target_link_libraries(profile PRIVATE satzip)
target_compile_options(profile PRIVATE ${SATZIP_OPT})

find_package(Threads REQUIRED)

option(SATZIP_WITH_7Z "Download LZMA SDK for .7z extract support" ON)
if(SATZIP_WITH_7Z)
    include(FetchContent)
    FetchContent_Declare(lzmasdk
        GIT_REPOSITORY https://github.com/jljusten/LZMA-SDK.git
        GIT_TAG 781863cdf592da3e97420f50de5dac056ad352a5)
    FetchContent_MakeAvailable(lzmasdk)
    if(NOT EXISTS ${lzmasdk_SOURCE_DIR}/C/7zArcIn.c)
        message(WARNING "LZMA SDK fetch failed; building without .7z support")
        set(SATZIP_WITH_7Z OFF)
    else()
        set(SZ7Z_SOURCES
            ${lzmasdk_SOURCE_DIR}/C/7zAlloc.c ${lzmasdk_SOURCE_DIR}/C/7zArcIn.c
            ${lzmasdk_SOURCE_DIR}/C/7zBuf.c ${lzmasdk_SOURCE_DIR}/C/7zBuf2.c
            ${lzmasdk_SOURCE_DIR}/C/7zCrc.c ${lzmasdk_SOURCE_DIR}/C/7zCrcOpt.c
            ${lzmasdk_SOURCE_DIR}/C/7zDec.c ${lzmasdk_SOURCE_DIR}/C/7zStream.c
            ${lzmasdk_SOURCE_DIR}/C/Bcj2.c ${lzmasdk_SOURCE_DIR}/C/Bra.c
            ${lzmasdk_SOURCE_DIR}/C/Bra86.c ${lzmasdk_SOURCE_DIR}/C/BraIA64.c
            ${lzmasdk_SOURCE_DIR}/C/CpuArch.c ${lzmasdk_SOURCE_DIR}/C/Delta.c
            ${lzmasdk_SOURCE_DIR}/C/Lzma2Dec.c ${lzmasdk_SOURCE_DIR}/C/LzmaDec.c
            ${lzmasdk_SOURCE_DIR}/C/LzmaEnc.c ${lzmasdk_SOURCE_DIR}/C/LzFind.c
            ${lzmasdk_SOURCE_DIR}/C/Lzma2Enc.c)
    endif()
endif()

option(SATZIP_WITH_ZIP "Download miniz for .zip extract support" ON)
if(SATZIP_WITH_ZIP)
    set(MINIZ_DIR ${CMAKE_BINARY_DIR}/_miniz)
    if(NOT EXISTS ${MINIZ_DIR}/miniz.c)
        file(DOWNLOAD https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
             ${MINIZ_DIR}/miniz.zip STATUS _mst)
        list(GET _mst 0 _mcode)
        if(_mcode EQUAL 0)
            file(ARCHIVE_EXTRACT INPUT ${MINIZ_DIR}/miniz.zip DESTINATION ${MINIZ_DIR})
        endif()
        if(NOT EXISTS ${MINIZ_DIR}/miniz.c)
            message(WARNING "miniz download failed; building without .zip support")
            set(SATZIP_WITH_ZIP OFF)
        endif()
    endif()
endif()

add_executable(satzip_cli app/cli/satzip.cpp ${SATZIP_WIN_RC} ${SATZIP_WIN_MANIFEST})
set_target_properties(satzip_cli PROPERTIES OUTPUT_NAME satzip)
target_include_directories(satzip_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app/common)
target_link_libraries(satzip_cli PRIVATE satzip Threads::Threads)
target_compile_options(satzip_cli PRIVATE ${SATZIP_OPT})
if(SATZIP_WITH_ZIP)
    target_sources(satzip_cli PRIVATE ${MINIZ_DIR}/miniz.c)
    target_include_directories(satzip_cli PRIVATE ${MINIZ_DIR})
    target_compile_definitions(satzip_cli PRIVATE SATZIP_HAVE_ZIP=1)
endif()
if(SATZIP_WITH_7Z)
    target_sources(satzip_cli PRIVATE ${SZ7Z_SOURCES})
    target_include_directories(satzip_cli PRIVATE ${lzmasdk_SOURCE_DIR}/C)
    target_compile_definitions(satzip_cli PRIVATE SATZIP_HAVE_7Z=1 _7ZIP_ST=1)
endif()
if(WIN32)
    target_link_libraries(satzip_cli PRIVATE shell32)
endif()

option(SATZIP_WITH_LZ4 "Download & link LZ4 v1.9.4 for comparison" ON)

add_executable(benchmark test/benchmark.c)
target_link_libraries(benchmark PRIVATE satzip_bench)
target_compile_options(benchmark PRIVATE ${SATZIP_OPT})

if(SATZIP_WITH_LZ4)
    set(LZ4_DIR ${CMAKE_BINARY_DIR}/_lz4)
    file(MAKE_DIRECTORY ${LZ4_DIR})
    foreach(f lz4.c lz4.h)
        if(NOT EXISTS ${LZ4_DIR}/${f})
            file(DOWNLOAD
                https://raw.githubusercontent.com/lz4/lz4/v1.9.4/lib/${f}
                ${LZ4_DIR}/${f} STATUS _st)
            list(GET _st 0 _code)
            if(NOT _code EQUAL 0)
                message(WARNING "LZ4 download failed (${_st}); benchmark builds without LZ4")
                set(SATZIP_WITH_LZ4 OFF)
            endif()
        endif()
    endforeach()
endif()
if(SATZIP_WITH_LZ4)
    target_sources(benchmark PRIVATE ${LZ4_DIR}/lz4.c)
    target_include_directories(benchmark PRIVATE ${LZ4_DIR})
    target_compile_definitions(benchmark PRIVATE HAVE_LZ4=1)
endif()

option(SATZIP_BUILD_GUI "Build the Dear ImGui + ImPlot desktop UI" OFF)
if(SATZIP_BUILD_GUI)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    include(FetchContent)

    FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(imgui  GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5)
    FetchContent_Declare(implot GIT_REPOSITORY https://github.com/epezent/implot.git GIT_TAG v0.16)
    FetchContent_MakeAvailable(glfw imgui implot)

    find_package(OpenGL REQUIRED)

    add_executable(satzip_gui
        app/gui/main.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
        ${SATZIP_WIN_RC} ${SATZIP_WIN_MANIFEST})
    target_compile_definitions(satzip_gui PRIVATE IMGUI_USE_WCHAR32)
    target_include_directories(satzip_gui PRIVATE
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${implot_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/app/common)
    target_link_libraries(satzip_gui PRIVATE satzip_bench glfw OpenGL::GL Threads::Threads)

    if(SATZIP_WITH_LZ4)
        target_sources(satzip_gui PRIVATE ${LZ4_DIR}/lz4.c)
        target_include_directories(satzip_gui PRIVATE ${LZ4_DIR})
        target_compile_definitions(satzip_gui PRIVATE HAVE_LZ4=1)
    endif()
    if(SATZIP_WITH_ZIP)
        target_sources(satzip_gui PRIVATE ${MINIZ_DIR}/miniz.c)
        target_include_directories(satzip_gui PRIVATE ${MINIZ_DIR})
        target_compile_definitions(satzip_gui PRIVATE SATZIP_HAVE_ZIP=1)
    endif()
    if(SATZIP_WITH_7Z)
        target_sources(satzip_gui PRIVATE ${SZ7Z_SOURCES})
        target_include_directories(satzip_gui PRIVATE ${lzmasdk_SOURCE_DIR}/C)
        target_compile_definitions(satzip_gui PRIVATE SATZIP_HAVE_7Z=1 _7ZIP_ST=1)
    endif()

     if(WIN32)
        set_target_properties(satzip_gui PROPERTIES WIN32_EXECUTABLE TRUE)
        target_link_libraries(satzip_gui PRIVATE comdlg32 comctl32 shell32 ole32 windowsapp)  # dialogs + WinRT share
        if(MSVC)
            target_link_options(satzip_gui PRIVATE /ENTRY:mainCRTStartup)
            target_compile_definitions(satzip_gui PRIVATE _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
        endif()
    endif()

    if(APPLE)
        set(SATZIP_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.icns)
        set_source_files_properties(${SATZIP_ICNS} PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
        target_sources(satzip_gui PRIVATE ${SATZIP_ICNS})
        set_target_properties(satzip_gui PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_ICON_FILE icon.icns)
        install(TARGETS satzip_gui BUNDLE DESTINATION .)
    else()
        install(TARGETS satzip_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

enable_testing()
add_test(NAME roundtrip COMMAND roundtrip)

install(TARGETS satzip_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS satzip ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/satzip.h include/satzip_stream.h include/satzip_bench.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})

set(CPACK_PACKAGE_NAME            "SAT-ZIP")
set(CPACK_PACKAGE_VENDOR          "Samalth")
set(CPACK_PACKAGE_VERSION         "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Ultra-high-throughput LZ codec (CLI + GUI)")
set(CPACK_PACKAGE_INSTALL_DIRECTORY   "SAT-ZIP")
set(CPACK_RESOURCE_FILE_LICENSE   "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README    "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_VERBATIM_VARIABLES      ON)
set(CPACK_PACKAGE_FILE_NAME       "SAT-ZIP-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")

if(SATZIP_BUILD_GUI)
    set(CPACK_PACKAGE_EXECUTABLES "satzip_gui" "SAT-ZIP")
endif()

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    find_program(SATZIP_MAKENSIS makensis
        PATHS "C:/Program Files (x86)/NSIS" "C:/Program Files/NSIS")
    if(SATZIP_MAKENSIS)
        list(APPEND CPACK_GENERATOR "NSIS")
    else()
        message(STATUS "makensis not found -> packaging ZIP only "
                       "(install NSIS for a .exe installer)")
    endif()
    set(CPACK_NSIS_MODIFY_PATH OFF)    # NSIS modpath breaks on long PATH; installer is Inno (installer/satzip.iss)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_DISPLAY_NAME "SAT-ZIP")
    set(CPACK_NSIS_MUI_ICON   "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\satzip.exe")
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "ExecWait '\\\"$INSTDIR\\\\bin\\\\satzip.exe\\\" --register'")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "ExecWait '\\\"$INSTDIR\\\\bin\\\\satzip.exe\\\" --unregister'")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;productbuild")
else()
    set(CPACK_GENERATOR "STGZ;TGZ")
endif()

include(CPack)
