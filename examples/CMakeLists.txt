add_executable(L_bracket L_bracket.cpp)
target_link_libraries(L_bracket threed_beam_fea)

add_executable(cantilever_beam_with_ties cantilever_beam_with_ties.cpp)
target_link_libraries(cantilever_beam_with_ties threed_beam_fea)
