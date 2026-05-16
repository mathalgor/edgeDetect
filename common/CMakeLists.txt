add_library(edgeDetectCommon STATIC
        AppConfig.cpp
        AppConfig.h
        CursorUtils.cpp
        CursorUtils.h
)

target_link_libraries(edgeDetectCommon PUBLIC Qt6::Core Qt6::Gui)
target_include_directories(edgeDetectCommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
