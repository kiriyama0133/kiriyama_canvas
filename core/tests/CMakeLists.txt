add_executable(kiriyama_core_tests
    ColorTest.cc
    DataTest.cc
    HitTestTest.cc
    InteractionTest.cc
    LayoutTest.cc
    SceneDiffTest.cc
    SceneBuilderTest.cc
    SceneTest.cc
    SparkBarTest.cc
    TableTest.cc
    TableLayoutTest.cc
    TooltipTest.cc
    VirtualScrollTest.cc
)

target_link_libraries(kiriyama_core_tests PRIVATE kiriyama_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(kiriyama_core_tests)
