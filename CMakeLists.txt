cmake_minimum_required(VERSION 3.16)

set(EXTENSION_NAME wiggle)

file(GLOB WIGGLE_EXTENSION_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")
set(WIGGLE_EXTENSION_OBJECT_FILES ${WIGGLE_EXTENSION_SOURCES})

build_extension_lib("${EXTENSION_NAME}")

target_include_directories(neug_${EXTENSION_NAME}_extension PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(neug_${EXTENSION_NAME}_extension PRIVATE neug)

if(BUILD_TEST)
    add_extension_test(NAME ${EXTENSION_NAME}
        TEST_SRCS
            ${CMAKE_CURRENT_SOURCE_DIR}/test/wiggle_test.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/test/ic2_test.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/test/ic3_test.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/test/ic_test.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/test/is_test.cc
    )
    target_compile_definitions(${EXTENSION_NAME}_extension_test PRIVATE
        WIGGLE_EXTENSION_LIB="${CMAKE_BINARY_DIR}/extension/${EXTENSION_NAME}/lib${EXTENSION_NAME}.neug_extension")
endif()
