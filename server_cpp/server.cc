#include "file_processor.grpc.pb.h"
#include "file_processor_service_impl.h"
#include <grpcpp/grpcpp.h>
#include <iostream>

using grpc::Server;
using grpc::ServerBuilder;

void RunServer() {
  std::string server_address("0.0.0.0:50051"); // bind em todas interfaces
  FileProcessorServiceImpl service;

  grpc::ServerBuilder builder;
  // Escutar em todas as interfaces para aceitar conexões locais/externas
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    std::cerr << "Falha ao iniciar o servidor gRPC em " << server_address << std::endl;
    return;
  }
  std::cout << "Servidor rodando e escutando em " << server_address << std::endl;

  server->Wait();
}

int main() {
  RunServer();
  return 0;
}
