cmake_minimum_required(VERSION 3.16)
project(jic-server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find packages
find_package(Threads REQUIRED)

# Configure llama.cpp build options to avoid X11 dependencies
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "llama: build tests" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "llama: build examples" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "llama: build server example" FORCE)
set(LLAMA_NATIVE OFF CACHE BOOL "llama: enable -march=native flag" FORCE)
set(LLAMA_LTO OFF CACHE BOOL "llama: enable link time optimization" FORCE)
set(LLAMA_STATIC ON CACHE BOOL "llama: build static library" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "build shared libraries" FORCE)
set(GGML_CCACHE OFF CACHE BOOL "ggml: disable ccache warning" FORCE)

# Enable parallel builds for llama.cpp
set(CMAKE_BUILD_PARALLEL_LEVEL ${CMAKE_BUILD_PARALLEL_LEVEL})

# Add llama.cpp as a subdirectory
add_subdirectory(llama.cpp)

# Add nlohmann/json
include(FetchContent)
set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
    GIT_SHALLOW TRUE
    GIT_CONFIG "http.sslVerify=false"
)
FetchContent_MakeAvailable(json)

# Main executable
add_executable(jic-server server.cpp)

# Link libraries
target_link_libraries(jic-server 
    PRIVATE 
    llama 
    nlohmann_json::nlohmann_json
    Threads::Threads
    curl
)

# Include directories
target_include_directories(jic-server PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp
)
