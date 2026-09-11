cmake_minimum_required(VERSION 3.19)
project(UartX VERSION 1.1.0 LANGUAGES CXX)

# Qt 6.2 is the floor so the Qt versions bundled with current LTS toolchains
# work unchanged (Ubuntu 22.04 ships 6.2; Debian 12 and Ubuntu 24.04 ship 6.4).
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
# Shared by the executable metadata, the About box, the installer and the
# Linux desktop entry, so the version and the publisher are stated once.
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
# Everything is platform-independent except the serial backend: the operating
# system differences are confined to src/serialport_{win,posix}.cpp behind the
# src/serialport.h interface.
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
    src/lineformat.cpp          src/lineformat.h
    src/lineassembler.cpp       src/lineassembler.h
    src/configstore.cpp         src/configstore.h
    src/appconstants.h
)

if(WIN32)
    list(APPEND UARTX_SOURCES src/serialport_win.cpp)
else()
    list(APPEND UARTX_SOURCES src/serialport_posix.cpp)
endif()

# WIN32 selects the GUI subsystem on Windows, so launching the app never opens
# a console window alongside it. It is ignored on other platforms.
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

# --- Windows: executable icon + version metadata (Properties > Details)
if(WIN32)
    set(APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/uartx.ico")
    configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(UartX PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------

if(WIN32)
    # Windows apps expect the Qt DLLs beside the executable, not in a bin/
    # subfolder. Both the install rule and windeployqt read these, so they have
    # to be set before either is declared or the two disagree and the app
    # cannot resolve Qt6Widgets.dll at startup.
    set(CMAKE_INSTALL_BINDIR ".")
    set(QT_DEPLOY_BIN_DIR ".")
endif()

install(TARGETS UartX
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WIN32)
    # windeployqt copies the Qt runtime next to the executable so the result is
    # self-contained and needs no Qt installation on the target machine. On
    # Linux the packages depend on the distribution's Qt instead, and the
    # AppImage script does its own bundling.
    if(COMMAND qt_generate_deploy_app_script)
        qt_generate_deploy_app_script(
            TARGET UartX
            OUTPUT_SCRIPT deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
        # QtDeploySupport.cmake only honours QT_DEPLOY_BIN_DIR if it is already
        # set when the install script runs -- setting it at configure time is
        # too late. Without this windeployqt drops the Qt DLLs into bin/ while
        # the exe sits at the prefix root, and the installed app cannot resolve
        # Qt6Widgets.dll.
        install(CODE "set(QT_DEPLOY_BIN_DIR \".\")")
        install(SCRIPT ${deploy_script})
    endif()
else()
    # --- Linux desktop integration -----------------------------------------
    configure_file(packaging/linux/uartx.desktop.in
                   "${CMAKE_CURRENT_BINARY_DIR}/${APP_DESKTOP_ID}.desktop" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${APP_DESKTOP_ID}.desktop"
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

    foreach(icon_size 16 24 32 48 64 128 256)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/uartx_${icon_size}.png"
                DESTINATION
                    ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${icon_size}x${icon_size}/apps
                RENAME ${APP_DESKTOP_ID}.png)
    endforeach()

    configure_file(packaging/linux/metainfo.xml.in
                   "${CMAKE_CURRENT_BINARY_DIR}/${APP_DESKTOP_ID}.metainfo.xml" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${APP_DESKTOP_ID}.metainfo.xml"
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
endif()

# ---------------------------------------------------------------------------
# Packaging
# ---------------------------------------------------------------------------

if(WIN32)
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
else()
    # --- Linux packages:  cd build/linux && cpack -G DEB   (also TGZ, RPM)
    set(CPACK_PACKAGE_NAME "uartx")
    set(CPACK_PACKAGE_VENDOR "${APP_COMPANY}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${APP_DESCRIPTION}")
    set(CPACK_PACKAGE_CONTACT "${APP_COMPANY} <${APP_SUPPORT}>")
    set(CPACK_PACKAGE_HOMEPAGE_URL "${APP_HOMEPAGE}")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_STRIP_FILES ON)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

    set(CPACK_DEBIAN_PACKAGE_SECTION "electronics")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    # Let CPack read the real Qt sonames off the binary rather than pinning a
    # version that would only match one distribution.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    # The Wayland platform plugin lives in its own package that the Qt base
    # install does not pull in. UartX falls back to X11 without it, so this is
    # a recommendation rather than a dependency -- but on a Wayland desktop the
    # native plugin is the better experience.
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")

    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

    set(CPACK_GENERATOR "TGZ")
    include(CPack)
endif()
