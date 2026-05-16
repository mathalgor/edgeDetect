add_library(edgeDetectCommon STATIC
        AppConfig.cpp
        AppConfig.h
)

target_link_libraries(edgeDetectCommon PUBLIC Qt6::Core)
target_include_directories(edgeDetectCommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
