find_package(Git REQUIRED)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

set(CMAKE_CXX_STANDARD 17)
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /W4 /WX")
else(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -O3 -pedantic")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb3")
    endif (CMAKE_BUILD_TYPE STREQUAL "Debug")
endif(MSVC)

if (EXP_FS)
    add_definitions(-DEXP_FS)
    message(STATUS "Using compatibility mode with std::experimental::filesystem support")
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DCOMPROENV_DEBUG)
endif (CMAKE_BUILD_TYPE STREQUAL "Debug")
