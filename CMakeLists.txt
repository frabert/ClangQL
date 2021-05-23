cmake_minimum_required(VERSION 3.5.1)
project(clangql VERSION 0.1.0
                DESCRIPTION "ClangQL"
                LANGUAGES C CXX)

find_package(Threads REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

add_library(clangql
  SHARED
    src/clangql.cc
    src/ClangQLModule.cc
    src/Index.grpc.pb.cc
    src/Index.pb.cc
    src/Service.grpc.pb.cc
    src/Service.pb.cc
    src/RefsTable.cc
    src/RelationsTable.cc
    src/SymbolsTable.cc
    src/Module.cc)

target_link_libraries(clangql PRIVATE protobuf::libprotobuf gRPC::grpc++)