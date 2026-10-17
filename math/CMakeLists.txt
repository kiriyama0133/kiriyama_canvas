set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(kiriyama_math STATIC
    src/Easing.cc
    src/Spring.cc
    src/Ticks.cc
)

target_include_directories(kiriyama_math PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(kiriyama_math PUBLIC cxx_std_23)

if(TARGET GTest::gtest_main)
    add_subdirectory(tests)
endif()
