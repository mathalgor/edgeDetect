add_executable(align
        main.cpp
        AlignMainWindow.cpp
        AlignMainWindow.h
        AlignViewWidget.cpp
        AlignViewWidget.h
        ProjectConfig.cpp
        ProjectConfig.h
        ProjectDialog.cpp
        ProjectDialog.h
)

target_link_libraries(align PRIVATE
        edgeDetectCommon
        Qt6::Widgets
        ${OpenCV_LIBS}
)
