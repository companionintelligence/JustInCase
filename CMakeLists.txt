cmake_minimum_required(VERSION 3.16)
project(jic-server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# sqlite-vec is embedded statically and its header pulls in sqlite3ext.h; without
# SQLITE_CORE that header routes every sqlite3_* call through an undeclared
# `sqlite3_api` pointer. Both the sqlite-vec library AND the app sources
# (server.cpp / ingestion.cpp include sqlite-vec.h) need it, so define it globally.
add_compile_definitions(SQLITE_CORE)

find_package(Threads REQUIRED)

# ── llama.cpp build options ───────────────────────────────────────────
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_NATIVE         OFF CACHE BOOL "" FORCE)
set(LLAMA_LTO            OFF CACHE BOOL "" FORCE)
set(LLAMA_STATIC         ON  CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
set(GGML_CCACHE          OFF CACHE BOOL "" FORCE)

# ── Header-only libraries (nlohmann/json, cpp-httplib) ───────────────
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(httplib INTERFACE)
target_include_directories(httplib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── SQLite (system or bundled amalgamation) ──────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3.c")
    add_library(sqlite3 STATIC vendor/sqlite3.c)
    target_include_directories(sqlite3 PUBLIC vendor)
    target_compile_definitions(sqlite3 PRIVATE
        SQLITE_ENABLE_FTS5
        SQLITE_ENABLE_JSON1
    )
else()
    find_library(SQLITE3_LIB sqlite3 REQUIRED)
    add_library(sqlite3 INTERFACE)
    target_link_libraries(sqlite3 INTERFACE ${SQLITE3_LIB})
endif()

# ── sqlite-vec (amalgamation) ────────────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite-vec.c")
    add_library(sqlite_vec STATIC vendor/sqlite-vec.c)
    target_include_directories(sqlite_vec PUBLIC vendor)
    target_link_libraries(sqlite_vec PUBLIC sqlite3)
    # Statically linked (not a loadable extension), so compile against the core
    # SQLite API directly. Without SQLITE_CORE, sqlite3ext.h routes every call
    # through an undeclared `sqlite3_api` pointer and the build fails with
    # "'sqlite3_api' was not declared in this scope".
    target_compile_definitions(sqlite_vec PRIVATE SQLITE_CORE)
else()
    message(WARNING "vendor/sqlite-vec.c not found — "
                    "sqlite-vec must be provided at build time")
endif()

# ── MuPDF (pre-built in Docker, or system) ───────────────────────────
if(EXISTS "/mupdf-install/lib/libmupdf.a")
    # Docker build: use pre-built MuPDF
    add_library(mupdf STATIC IMPORTED)
    set_target_properties(mupdf PROPERTIES
        IMPORTED_LOCATION /mupdf-install/lib/libmupdf.a)
    add_library(mupdf_third STATIC IMPORTED)
    set_target_properties(mupdf_third PROPERTIES
        IMPORTED_LOCATION /mupdf-install/lib/libmupdf-third.a)

    set(MUPDF_INCLUDE_DIR /mupdf-install/include)
    set(MUPDF_LIBS mupdf mupdf_third)
else()
    # Local build: expect system MuPDF
    find_library(MUPDF_LIB mupdf)
    find_library(MUPDF_THIRD_LIB mupdf-third)
    find_path(MUPDF_INCLUDE_DIR mupdf/fitz.h)
    if(MUPDF_LIB AND MUPDF_INCLUDE_DIR)
        add_library(mupdf STATIC IMPORTED)
        set_target_properties(mupdf PROPERTIES IMPORTED_LOCATION ${MUPDF_LIB})
        if(MUPDF_THIRD_LIB)
            add_library(mupdf_third STATIC IMPORTED)
            set_target_properties(mupdf_third PROPERTIES
                IMPORTED_LOCATION ${MUPDF_THIRD_LIB})
            set(MUPDF_LIBS mupdf mupdf_third)
        else()
            set(MUPDF_LIBS mupdf)
        endif()
    else()
        message(FATAL_ERROR "MuPDF not found. Install libmupdf-dev or build in Docker.")
    endif()
endif()

# ── llama.cpp (pre-built in Docker, or from source) ──────────────────
if(EXISTS "/llama-install/lib/libllama.a")
    add_library(llama STATIC IMPORTED)
    set_target_properties(llama PROPERTIES
        IMPORTED_LOCATION /llama-install/lib/libllama.a
        INTERFACE_INCLUDE_DIRECTORIES /llama-install/include)
    file(GLOB GGML_LIBS "/llama-install/lib/libggml*.a")
    foreach(lp ${GGML_LIBS})
        get_filename_component(ln ${lp} NAME_WE)
        add_library(${ln} STATIC IMPORTED)
        set_target_properties(${ln} PROPERTIES IMPORTED_LOCATION ${lp})
    endforeach()
    set(LLAMA_INCLUDE /llama-install/include)
else()
    add_subdirectory(llama.cpp)
    set(LLAMA_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp)
endif()

# ── Executables ──────────────────────────────────────────────────────

add_executable(jic-server    src/server.cpp)
add_executable(jic-ingestion src/ingestion.cpp)

# Common include dirs
set(JIC_INCLUDES
    ${LLAMA_INCLUDE}
    ${MUPDF_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Common link libraries
if(EXISTS "/llama-install/lib/libllama.a")
    set(LLAMA_LINK_LIBS
        -Wl,--whole-archive
        /llama-install/lib/libllama.a
        /llama-install/lib/libcommon.a
        /llama-install/lib/libggml-base.a
        /llama-install/lib/libggml-cpu.a
        /llama-install/lib/libggml.a
        -Wl,--no-whole-archive
    )
else()
    set(LLAMA_LINK_LIBS llama ggml)
endif()

set(COMMON_LIBS
    ${LLAMA_LINK_LIBS}
    ${MUPDF_LIBS}
    sqlite_vec
    sqlite3
    nlohmann_json
    httplib
    Threads::Threads
    m dl stdc++ pthread gomp
)

# ── jic-server ───────────────────────────────────────────────────────
target_include_directories(jic-server PRIVATE ${JIC_INCLUDES})
target_link_libraries(jic-server PRIVATE ${COMMON_LIBS})

# ── jic-ingestion ────────────────────────────────────────────────────
target_include_directories(jic-ingestion PRIVATE ${JIC_INCLUDES})
target_link_libraries(jic-ingestion PRIVATE ${COMMON_LIBS})

# ── Build info ───────────────────────────────────────────────────────
message(STATUS "JIC build configuration:")
message(STATUS "  CMAKE_BUILD_TYPE : ${CMAKE_BUILD_TYPE}")
message(STATUS "  MuPDF include    : ${MUPDF_INCLUDE_DIR}")
