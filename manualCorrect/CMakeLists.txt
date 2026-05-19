add_executable(manualCorrect
        main.cpp
        McMainWindow.cpp
        McMainWindow.h
        McViewWidget.cpp
        McViewWidget.h
        ProjectConfig.cpp
        ProjectConfig.h
        ProjectDialog.cpp
        ProjectDialog.h
)

target_link_libraries(manualCorrect PRIVATE
        edgeDetectCommon
        Qt6::Widgets
        ${OpenCV_LIBS}
)