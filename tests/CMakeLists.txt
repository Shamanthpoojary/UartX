# Automated tests. Core-only: they link uartx_core, never the widgets, so they
# run headless on every CI platform.
find_package(Qt6 REQUIRED COMPONENTS Test)

function(uartx_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE uartx_core Qt::Test)
    add_test(NAME ${name} COMMAND ${name})
    # QTEST_GUILESS_MAIN needs no display, but QtGui is still loaded for
    # QColor; offscreen keeps a headless Linux runner from looking for X11.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

uartx_test(tst_lineassembler)
uartx_test(tst_textdecoder)
uartx_test(tst_logstore)
uartx_test(tst_pipeline)
