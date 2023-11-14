cmake_minimum_required (VERSION 3.16)
project(EMI CXX C)
include(string_to_list.cmake)
include(find_llvm.cmake)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")

set(LINK_LIBS
  clangTooling
  clangToolingASTDiff
  clangToolingRefactoring
  clangToolingCore
  clangASTMatchers
  clangFrontend
  clangDriver
  clangSerialization
  clangTransformer
  clangRewrite
  clangFormat
  clangToolingInclusions
  clangParse
  clangSema
  clangAnalysis
  clangEdit
  clangAST
  clangLex
  clangBasic
  clang
  LLVM)

file(GLOB_RECURSE SOURCES "*/*.cc")
add_executable(EMI ${SOURCES} main.cc shell.cc)
target_include_directories(EMI PRIVATE include)
target_link_libraries(EMI PRIVATE ${LINK_LIBS})
target_include_directories(EMI PRIVATE
  . ${LLVM_INCLUDE_DIRS})
target_link_directories(EMI PRIVATE
  ${LLVM_LIBRARY_DIRS})
target_compile_options(EMI PRIVATE -ggdb3)
