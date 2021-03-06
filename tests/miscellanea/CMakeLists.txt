# compiler and flags
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -finline-functions")

# macros to be provided to the compiler
add_definitions(-DFF_BOUNDED_BUFFER -DTRACE_WINDFLOW)

# header files of WindFlow and of FastFlow
include_directories(../../wf ${ff_root_dir})

# linking to pthread, gvc and cgraph
link_libraries(pthread gvc cgraph)

# set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../../bin/miscellanea)

# cpp files to be compiled
file(GLOB SOURCES "*.cpp")

# add a target for each cpp file and a unique target for all the tests in this folder
add_custom_target(miscellanea)
foreach(testsourcefile ${SOURCES})
    get_filename_component(barename ${testsourcefile} NAME)
    string(REPLACE ".cpp" "" testname ${barename})
    add_executable(${testname} ${testsourcefile})
    cotire(${testname})
    add_dependencies(miscellanea ${testname})
endforeach(testsourcefile ${SOURCES})
