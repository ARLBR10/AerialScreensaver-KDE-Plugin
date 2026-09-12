include(ECMQmlModule)

set(aerial_backend_sources
    aerialbackend.cpp
    aerialbackend.h
    catalogmodel.cpp
    catalogmodel.h
    manifestparser.cpp
    manifestparser.h
)

add_library(aerialbackend_static STATIC ${aerial_backend_sources})
set_property(TARGET aerialbackend_static PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(aerialbackend_static PUBLIC Qt6::Concurrent Qt6::Core Qt6::Network Qt6::Qml KF6::Archive)
target_include_directories(aerialbackend_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(AERIAL_ALLOW_INSECURE_TLS)
    target_compile_definitions(aerialbackend_static PRIVATE AERIAL_ALLOW_INSECURE_TLS)
endif()

ecm_add_qml_module(aerialbackendplugin
    URI "org.kde.plasma.aerial.private"
    VERSION 1.0
    CLASS_NAME AerialPlugin
)
qt_generate_foreign_qml_types(aerialbackend_static aerialbackendplugin)
target_sources(aerialbackendplugin PRIVATE plugin.cpp)
target_link_libraries(aerialbackendplugin PRIVATE aerialbackend_static)
ecm_finalize_qml_module(aerialbackendplugin)
