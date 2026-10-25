add_library(embercast_engine
  src/version.cpp
)
target_include_directories(embercast_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(embercast_engine PUBLIC Threads::Threads)

if(EMBERCAST_OPENCL)
  message(FATAL_ERROR "EMBERCAST_OPENCL: the OpenCL backend is Step 12. It is not implemented yet.")
endif()
