add_executable(kiriyama_math_tests
    EasingTest.cc
    InterpolationTest.cc
    MathTest.cc
    SpringTest.cc
)

target_link_libraries(kiriyama_math_tests PRIVATE kiriyama_math GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(kiriyama_math_tests)
