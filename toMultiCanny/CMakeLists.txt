add_executable(toMultiCanny
        main.cpp
        Canny.h
        Canny.cpp
)

target_link_libraries(toMultiCanny PRIVATE ${OpenCV_LIBS})
target_include_directories(toMultiCanny PRIVATE ${OpenCV_INCLUDE_DIRS})
