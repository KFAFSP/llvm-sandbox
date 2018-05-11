add_executable(bm-jit main.cpp)

target_link_libraries(bm-jit ${llvm_libs})
