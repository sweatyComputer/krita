set(kritadhwetpaintop_SOURCES
    dh_wetpaint_plugin.cpp
    KisWetPaintEngine.cpp
    KisWetPaintInterstrokeData.cpp
    KisWetPaintOp.cpp
    KisWetPaintOpSettings.cpp
    KisWetPaintOpSettingsWidget.cpp
    KisWetPaintOptionData.cpp
    KisWetPaintOptionModel.cpp
    KisWetPaintOptionWidget.cpp
)

kis_add_library(kritadhwetpaintop MODULE ${kritadhwetpaintop_SOURCES})

target_link_libraries(kritadhwetpaintop kritalibpaintop Qt${QT_MAJOR_VERSION}::Concurrent)

# The pigment maths is written to auto-vectorise; errno-free sqrt lets it.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(KisWetPaintEngine.cpp PROPERTIES COMPILE_OPTIONS "-fno-math-errno")
endif()

install(TARGETS kritadhwetpaintop DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
