add_executable(manifestparser_test manifestparser_test.cpp)
target_link_libraries(manifestparser_test PRIVATE aerialbackend_static Qt6::Test)
target_compile_definitions(manifestparser_test PRIVATE FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tvos.json")
add_test(NAME manifestparser_test COMMAND manifestparser_test)
