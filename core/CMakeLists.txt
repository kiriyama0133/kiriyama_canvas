set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(kiriyama_core STATIC
    src/Axis.cc
    src/Data.cc
    src/Geometry.cc
    src/HitTest.cc
    src/Interaction.cc
    src/Layout.cc
    src/NodeType.cc
    src/SceneBuilder.cc
    src/SceneDiff.cc
    src/SceneNode.cc
    src/SceneQuery.cc
    src/ScrollLayout.cc
    src/SparkBar.cc
    src/Table.cc
    src/TableLayout.cc
    src/Theme.cc
    src/Tooltip.cc
    src/VirtualScroll.cc
)

target_include_directories(kiriyama_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(kiriyama_core PUBLIC kiriyama_math)
target_compile_features(kiriyama_core PUBLIC cxx_std_23)

if(TARGET GTest::gtest_main)
    add_subdirectory(tests)
endif()
