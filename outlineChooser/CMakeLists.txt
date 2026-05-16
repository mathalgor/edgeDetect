add_executable(outlineChooser
        main.cpp
        OcMainWindow.cpp
        OcMainWindow.h
        OcViewWidget.cpp
        OcViewWidget.h
        ProjectConfig.cpp
        ProjectConfig.h
        ProjectDialog.cpp
        ProjectDialog.h
)

target_link_libraries(outlineChooser PRIVATE
        edgeDetectCommon
        Qt6::Widgets
        ${OpenCV_LIBS}
)
