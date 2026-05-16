find_package(OpenCV REQUIRED)

add_library(edgeDetectCommon STATIC
        AppConfig.cpp
        AppConfig.h
        CursorUtils.cpp
        CursorUtils.h
        OriginalLoader.cpp
        OriginalLoader.h
        TimeTracker.cpp
        TimeTracker.h
        ViewPreset.h
)

target_link_libraries(edgeDetectCommon PUBLIC Qt6::Core Qt6::Gui ${OpenCV_LIBS})
target_include_directories(edgeDetectCommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
