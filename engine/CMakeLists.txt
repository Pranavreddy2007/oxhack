add_library(embercast_engine
  src/version.cpp
  src/model.cpp
  src/frame.cpp
  src/forecast.cpp
  src/filter.cpp
  src/policy.cpp
  src/engine.cpp
  src/transport.cpp
  backends/cpu_naive/cpu_naive.cpp
  backends/cpu_opt/cpu_opt.cpp
)
target_include_directories(embercast_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(embercast_engine PUBLIC Threads::Threads)

if(EMBERCAST_OPENCL)
  message(FATAL_ERROR "EMBERCAST_OPENCL: the OpenCL backend is Step 12. It is not implemented yet.")
endif()
