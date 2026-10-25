add_library(embercast_sim
  scenario.cpp
  truth.cpp
)
target_include_directories(embercast_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(embercast_sim PUBLIC embercast_engine)
