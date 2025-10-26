cmake_minimum_required(VERSION 3.10)
project(grpc_server)

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/file_processor.pb.cc")
set(PROTO_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/file_processor.pb.h")
set(GRPC_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/file_processor.grpc.pb.cc")
set(GRPC_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/file_processor.grpc.pb.h")

add_executable(server server.cc file_processor_service_impl.cc ${PROTO_SRCS} ${GRPC_SRCS})
target_link_libraries(server gRPC::grpc++ protobuf::libprotobuf)
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
