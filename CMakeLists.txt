cmake_minimum_required(VERSION 3.19)
project(UartX VERSION 1.0.0 LANGUAGES CXX)

# UartX currently targets Windows only. The serial layer behind serialport.h
# was written to be portable, but only the Win32 backend ships in this
# repository, so fail early with an explanation rather than part-way through
# the build with a missing source file.
if(NOT WIN32)
    message(FATAL_ERROR
        "UartX targets Windows only.\n"
        "The POSIX serial backend is not part of this repository; see "
        "docs/BUILDING.md for the platform notes.")
endif()

# Qt 6.2 is the floor so the Qt versions bundled with current LTS toolchains
# work unchanged.
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

# qt_standard_project_setup() arrived in Qt 6.3; do the same by hand when the
# available Qt predates it.
if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
else()
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Product identity
#
# Shared by the executable metadata, the About box and the installer, so the
# version and the publisher are stated in exactly one place.
# ---------------------------------------------------------------------------

set(APP_PRODUCT     "UartX")
set(APP_COMPANY     "Shamanth")
set(APP_DESCRIPTION "UART debugging and log analysis tool for firmware developers")
set(APP_COPYRIGHT   "Copyright (C) 2026 Shamanth")
set(APP_PUBLISHER   "${APP_COMPANY}")
set(APP_SUPPORT     "shamanth25402@gmail.com")
set(APP_HOMEPAGE    "https://github.com/Shamanthpoojary/UartX")
set(APP_DESKTOP_ID  "uartx")

# ---------------------------------------------------------------------------
# Sources
#
# Everything is platform-independent except the serial backend, which is
# confined to src/serialport_win.cpp behind the src/serialport.h interface.
# ---------------------------------------------------------------------------

set(UARTX_SOURCES
    src/main.cpp
    src/mainwindow.cpp          src/mainwindow.h
    src/terminalview.cpp        src/terminalview.h
    src/filterwindow.cpp        src/filterwindow.h
    src/colorrulesdialog.cpp    src/colorrulesdialog.h
    src/colorrules.cpp          src/colorrules.h
    src/textfilters.cpp         src/textfilters.h
    src/ribbon.cpp              src/ribbon.h
    src/theme.cpp               src/theme.h
    src/dialogs.cpp             src/dialogs.h
    src/serialworker.cpp        src/serialworker.h
    src/serialport.h
    src/serialport_win.cpp
    src/lineformat.cpp          src/lineformat.h
    src/lineassembler.cpp       src/lineassembler.h
    src/configstore.cpp         src/configstore.h
    src/appconstants.h
)

# WIN32 selects the GUI subsystem, so launching the app never opens a console
# window alongside it.
qt_add_executable(UartX WIN32 ${UARTX_SOURCES})

target_include_directories(UartX PRIVATE src)

target_compile_definitions(UartX PRIVATE
    APP_VERSION_STRING="${PROJECT_VERSION}"
)

# BASE keeps the resource aliases at :/icons/<file>.png, so moving the files
# into resources/icons/ did not change any path the C++ asks for.
qt_add_resources(UartX "app_resources"
    PREFIX "/icons"
    BASE "resources/icons"
    FILES
        resources/icons/uartx_16.png
        resources/icons/uartx_24.png
        resources/icons/uartx_32.png
        resources/icons/uartx_48.png
        resources/icons/uartx_64.png
        resources/icons/uartx_128.png
        resources/icons/uartx_256.png
        resources/icons/uartx_logo.png
        resources/icons/chevron_down.png
        resources/icons/chevron_down_dim.png
        resources/icons/check.png
)

target_link_libraries(UartX PRIVATE Qt::Core Qt::Gui Qt::Widgets)

# --- Executable icon and version metadata (Properties > Details)
set(APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/uartx.ico")
configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
target_sources(UartX PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------

# Windows apps expect the Qt DLLs beside the executable, not in a bin/
# subfolder. Both the install rule and windeployqt read these, so they have to
# be set before either is declared or the two disagree and the app cannot
# resolve Qt6Widgets.dll at startup.
set(CMAKE_INSTALL_BINDIR ".")
set(QT_DEPLOY_BIN_DIR ".")

install(TARGETS UartX
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# windeployqt copies the Qt runtime next to the executable so the result is
# self-contained and needs no Qt installation on the target machine.
if(COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(
        TARGET UartX
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    # QtDeploySupport.cmake only honours QT_DEPLOY_BIN_DIR if it is already
    # set when the install script runs -- setting it at configure time is too
    # late. Without this windeployqt drops the Qt DLLs into bin/ while the exe
    # sits at the prefix root, and the installed app cannot resolve
    # Qt6Widgets.dll.
    install(CODE "set(QT_DEPLOY_BIN_DIR \".\")")
    install(SCRIPT ${deploy_script})
endif()

# ---------------------------------------------------------------------------
# Packaging
# ---------------------------------------------------------------------------

# --- Inno Setup installer:  cmake --build <dir> --target installer
find_program(ISCC_EXECUTABLE
    NAMES ISCC.exe
    PATHS
        "$ENV{ProgramFiles\(x86\)}/Inno Setup 6"
        "$ENV{ProgramFiles}/Inno Setup 6"
        "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
    DOC "Inno Setup command-line compiler"
)

set(INSTALLER_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")
set(INSTALLER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/installer")

configure_file(packaging/windows/installer.iss.in
               "${CMAKE_BINARY_DIR}/installer.iss" @ONLY)

if(ISCC_EXECUTABLE)
    add_custom_target(installer
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${INSTALLER_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
                --config $<CONFIG> --prefix "${INSTALLER_STAGE_DIR}"
        COMMAND "${ISCC_EXECUTABLE}" "/O${INSTALLER_OUTPUT_DIR}" "${CMAKE_BINARY_DIR}/installer.iss"
        DEPENDS UartX
        COMMENT "Staging a self-contained build and compiling the Inno Setup installer"
        VERBATIM
    )
else()
    add_custom_target(installer
        COMMAND ${CMAKE_COMMAND} -E echo
                "Inno Setup 6 was not found. Install it (winget install -e --id JRSoftware.InnoSetup),"
        COMMAND ${CMAKE_COMMAND} -E echo
                "then re-run CMake so ISCC_EXECUTABLE is picked up."
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "Inno Setup not found"
        VERBATIM
    )
endif()

# --- Portable zip:  cmake --build <dir> --target portable
add_custom_target(portable
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${INSTALLER_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
            --config $<CONFIG> --prefix "${INSTALLER_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${INSTALLER_OUTPUT_DIR}"
    # chdir rather than WORKING_DIRECTORY: the stage dir does not exist yet
    # when the first command of this target runs.
    COMMAND ${CMAKE_COMMAND} -E chdir "${INSTALLER_STAGE_DIR}"
            ${CMAKE_COMMAND} -E tar cf
            "${INSTALLER_OUTPUT_DIR}/UartX-${PROJECT_VERSION}-portable-win64.zip"
            --format=zip .
    DEPENDS UartX
    COMMENT "Building the portable zip"
    VERBATIM
)
