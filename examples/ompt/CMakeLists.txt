if(HPXMP_WITH_OMPT)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -fopenmp")
    add_executable(hello_world hello_world.c)
endif()