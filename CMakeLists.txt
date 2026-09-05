cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBWEBP_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_webp.c
)

SET(LIBWEBP_LIBS
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libwebpdecoder.a
)

SET(LIBWEBP_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libwebp
        "${LIBWEBP_SRC}"
        "${LIBWEBP_LIBS}"
        []
        ""
        "${LIBWEBP_INC}"
        ""
        "1")
