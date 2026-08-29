set(kritatouchgestures_SOURCES
    TouchGestures.cpp
    KisTouchGestureSettings.cpp
    )

ki18n_wrap_ui(kritatouchgestures_SOURCES
    wdg_touchgesturesettings.ui
    )

kis_add_library(kritatouchgestures MODULE ${kritatouchgestures_SOURCES})
target_link_libraries(kritatouchgestures kritaui)
install(TARGETS kritatouchgestures DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
