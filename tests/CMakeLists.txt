add_executable(test_storage test_storage.cpp)
target_link_libraries(test_storage PRIVATE nvr_core)
add_test(NAME storage COMMAND test_storage)

add_executable(test_onvif test_onvif.cpp)
target_link_libraries(test_onvif PRIVATE nvr_core)
add_test(NAME onvif COMMAND test_onvif)

add_executable(test_gb28181 test_gb28181.cpp)
target_link_libraries(test_gb28181 PRIVATE nvr_core)
add_test(NAME gb28181 COMMAND test_gb28181)
