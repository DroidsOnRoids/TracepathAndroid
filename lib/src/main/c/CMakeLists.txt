cmake_minimum_required(VERSION 3.4.1)

add_library(tracepath
        SHARED
        main.c
        tracepath.c
        exception.c
        )

target_link_libraries(tracepath)