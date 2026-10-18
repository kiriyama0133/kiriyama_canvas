add_executable(kiriyama_text_tests
    AnimationTest.cc
    AppTest.cc
    DragTest.cc
    PainterTest.cc
    PieChartTest.cc
    SparkBarTest.cc
    TextLayoutTest.cc
    TtfFontTest.cc
    TooltipTargetTest.cc
    WidgetTest.cc
)

target_link_libraries(kiriyama_text_tests PRIVATE kiriyama_render kiriyama_scene kiriyama_fonts
    SDL3::SDL3 GTest::gtest_main)

# 字体测试要读被点名的 ttf：wasm 里靠 preload 打进虚拟文件系统，
# 和产物用同一套路径解析（/public/xxx.ttf）。测试也跟着 KIRIYAMA_FONTS 走，
# 这样"测过的字体"就是"打进产物的字体"，不会出现测试绿了但产物少字体。
# 注意产物是 kiriyama_text_tests.{js,wasm,data} 三件套，node 必须在本目录运行
# （.data 是按当前工作目录找的），ctest 默认就在可执行文件目录，符合。
kiriyama_preload_fonts(kiriyama_text_tests)

include(GoogleTest)
gtest_discover_tests(kiriyama_text_tests)
