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
  # The backend loads libOpenCL with dlopen() at run time: no SDK needed to build.
  target_sources(embercast_engine PRIVATE backends/opencl/opencl.cpp)
  target_compile_definitions(embercast_engine PUBLIC EMBERCAST_HAVE_OPENCL=1)
  target_link_libraries(embercast_engine PUBLIC ${CMAKE_DL_LIBS})
endif()
