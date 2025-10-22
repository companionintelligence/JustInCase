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

# Check if we're using pre-built llama (for Docker cache optimization)
if(EXISTS "/llama-install/lib/libllama.a")
    # Use pre-built llama library
    add_library(llama STATIC IMPORTED)
    set_target_properties(llama PROPERTIES
        IMPORTED_LOCATION /llama-install/lib/libllama.a
        INTERFACE_INCLUDE_DIRECTORIES /llama-install/include
    )
    
    # Import all ggml libraries
    file(GLOB GGML_LIBS "/llama-install/lib/libggml*.a")
    foreach(lib_path ${GGML_LIBS})
        get_filename_component(lib_name ${lib_path} NAME_WE)
        add_library(${lib_name} STATIC IMPORTED)
        set_target_properties(${lib_name} PROPERTIES
            IMPORTED_LOCATION ${lib_path}
        )
    endforeach()
else()
    # Build llama.cpp from source
    add_subdirectory(llama.cpp)
endif()

# Add nlohmann/json (already downloaded as single header)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Main executable
add_executable(jic-server server.cpp)

# Enable verbose output during compilation
set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "Enable verbose build output" FORCE)
set(CMAKE_RULE_MESSAGES ON CACHE BOOL "Enable rule messages" FORCE)

# Print build configuration
message(STATUS "Build configuration:")
message(STATUS "  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "  CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
message(STATUS "  CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")

# Link libraries (order matters for static linking)
if(EXISTS "/llama-install/lib/libllama.a")
    # When using pre-built libraries, link them explicitly in dependency order
    # Start with the most dependent libraries first
    set(LLAMA_LIBS "")
    
    # First, add the main llama library
    if(EXISTS "/llama-install/lib/libllama.a")
        list(APPEND LLAMA_LIBS "/llama-install/lib/libllama.a")
    endif()
    
    # Then add all ggml libraries in the correct order
    # The order matters: CPU backend must come before general backend
    file(GLOB GGML_LIBS "/llama-install/lib/libggml*.a")
    
    # Sort to ensure consistent order
    list(SORT GGML_LIBS)
    
    # Add common library if it exists
    if(EXISTS "/llama-install/lib/libcommon.a")
        list(APPEND LLAMA_LIBS "/llama-install/lib/libcommon.a")
    endif()
    
    target_link_libraries(jic-server 
        PRIVATE 
        ${LLAMA_LIBS}
        ${GGML_LIBS}
        nlohmann_json
        Threads::Threads
        curl
        openblas
        m  # math library
        dl  # for dlopen
        stdc++  # C++ standard library
        pthread  # POSIX threads
    )
    
    # Print the libraries being linked for debugging
    message(STATUS "Linking with libraries:")
    foreach(lib ${LLAMA_LIBS} ${GGML_LIBS})
        message(STATUS "  ${lib}")
    endforeach()
else()
    # When building from source
    target_link_libraries(jic-server 
        PRIVATE 
        llama
        ggml
        nlohmann_json
        Threads::Threads
        curl
        openblas
        m  # math library
        ${CMAKE_DL_LIBS}  # for dlopen if needed
    )
endif()

# Include directories
if(EXISTS "/llama-install/lib/libllama.a")
    # When using pre-built libraries
    target_include_directories(jic-server PRIVATE 
        /llama-install/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
else()
    # When building from source
    target_include_directories(jic-server PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
endif()
