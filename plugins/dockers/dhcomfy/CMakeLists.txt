# DH Comfy: run ComfyUI workflows on another machine from a Krita docker (DH-COMFY)
set(kritadhcomfy_SOURCES
    DhComfyPlugin.cpp
    DhComfyDock.cpp
    DhComfyClient.cpp
    DhComfyWorkflow.cpp
)
kis_add_library(kritadhcomfy MODULE ${kritadhcomfy_SOURCES})
target_link_libraries(kritadhcomfy kritaui Qt${QT_MAJOR_VERSION}::Network)
install(TARGETS kritadhcomfy DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
