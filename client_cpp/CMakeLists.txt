cmake_minimum_required(VERSION 3.10)
project(grpc_client)

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/file_processor.pb.cc")
set(PROTO_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/file_processor.pb.h")
set(GRPC_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/file_processor.grpc.pb.cc")
set(GRPC_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/file_processor.grpc.pb.h")

add_executable(client client.cc ${PROTO_SRCS} ${GRPC_SRCS})
target_link_libraries(client gRPC::grpc++ protobuf::libprotobuf)
target_include_directories(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
