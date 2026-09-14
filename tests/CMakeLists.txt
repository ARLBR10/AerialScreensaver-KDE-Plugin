add_executable(manifestparser_test manifestparser_test.cpp)
target_link_libraries(manifestparser_test PRIVATE aerialbackend_static Qt6::Test)
target_compile_definitions(manifestparser_test PRIVATE FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tvos.json")
add_test(NAME manifestparser_test COMMAND manifestparser_test)

add_executable(backend_test backend_test.cpp)
target_link_libraries(backend_test PRIVATE aerialbackend_static Qt6::Test)
add_test(NAME backend_test COMMAND backend_test)

add_executable(mainqml_test mainqml_test.cpp)
target_link_libraries(mainqml_test PRIVATE Qt6::Qml Qt6::Test)
target_compile_definitions(mainqml_test PRIVATE MAIN_QML_PATH="${CMAKE_SOURCE_DIR}/package/contents/ui/main.qml")
add_test(NAME mainqml_test COMMAND mainqml_test)
