add_executable(test_storage test_storage.cpp)
target_link_libraries(test_storage PRIVATE nvr_core)
add_test(NAME storage COMMAND test_storage)
