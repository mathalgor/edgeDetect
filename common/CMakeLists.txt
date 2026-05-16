find_package(OpenCV REQUIRED)

add_library(edgeDetectCommon STATIC
        AppConfig.cpp
        AppConfig.h
        CursorUtils.cpp
        CursorUtils.h
        EditColors.h
        OriginalLoader.cpp
        OriginalLoader.h
        ProjectTimeDialog.cpp
        ProjectTimeDialog.h
        TimeTracker.cpp
        TimeTracker.h
        ViewPreset.h
)

target_link_libraries(edgeDetectCommon PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets ${OpenCV_LIBS})
target_include_directories(edgeDetectCommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
