add_executable(embercast_tests
  test_main.cpp
  test_smoke.cpp
  test_rng_model.cpp
  test_frame.cpp
  test_truth.cpp
  test_filter_policy.cpp
  test_backends.cpp
)
target_include_directories(embercast_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(embercast_tests PRIVATE embercast_sim embercast_engine)
add_test(NAME embercast_tests COMMAND embercast_tests)
