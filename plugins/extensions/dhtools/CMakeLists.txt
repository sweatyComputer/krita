set(kritadhtools_SOURCES
    DhTools.cpp
    DhNodeUtils.cpp
    DhLayerCycle.cpp
    DhRadialConfig.cpp
    DhRadialDesign.cpp
    DhRadialIcons.cpp
    DhRadialPresets.cpp
    DhRadialLayout.cpp
    DhRadialMenu.cpp
    KisDhRadialSettings.cpp
    )

qt_add_resources(kritadhtools_SOURCES
    dhtools.qrc
)

kis_add_library(kritadhtools MODULE ${kritadhtools_SOURCES})
target_link_libraries(kritadhtools kritaui)
install(TARGETS kritadhtools DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})

install(FILES dhtools.xmlgui DESTINATION ${KDE_INSTALL_DATADIR}/kritaplugins)
install(FILES DhTools.action DESTINATION ${KDE_INSTALL_DATADIR}/krita/actions)
