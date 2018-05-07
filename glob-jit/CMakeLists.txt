add_executable(main main.cpp glob_pattern.cpp)

target_link_libraries(main ${llvm_libs})
