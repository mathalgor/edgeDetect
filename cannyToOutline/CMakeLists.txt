add_executable(cannyToOutline
        main.cpp
        CannyMainWindow.cpp
        CannyMainWindow.h
        CannyViewWidget.cpp
        CannyViewWidget.h
        ProjectConfig.cpp
        ProjectConfig.h
        ProjectDialog.cpp
        ProjectDialog.h
)

target_link_libraries(cannyToOutline PRIVATE
        edgeDetectCommon
        Qt6::Widgets
        ${OpenCV_LIBS}
)
