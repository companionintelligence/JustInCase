cmake_minimum_required(VERSION 3.16)
project(jic-server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(Threads REQUIRED)

# Add llama.cpp as a subdirectory
add_subdirectory(llama.cpp)

# Add Faiss
find_package(faiss REQUIRED)

# Add nlohmann/json
include(FetchContent)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json)

# Main executable
add_executable(jic-server server.cpp)

# Link libraries
target_link_libraries(jic-server 
    PRIVATE 
    llama 
    faiss
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# Include directories
target_include_directories(jic-server PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp
)
