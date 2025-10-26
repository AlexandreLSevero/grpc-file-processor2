#include <grpcpp/grpcpp.h>
#include <fstream>
#include <iostream>
#include <string>
#include "file_processor.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::ClientReaderWriter;
using file_processor::FileProcessorService;
using file_processor::RequestChunk;
using file_processor::ResponseChunk;
using file_processor::FileMetadata;
using file_processor::FileChunk;
using file_processor::StatusMessage;
using file_processor::CompressPDFParams;
using file_processor::ConvertToTXTParams;
using file_processor::ConvertImageFormatParams;
using file_processor::ResizeImageParams;

int main() {
  auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
  auto stub = FileProcessorService::NewStub(channel);

  std::cout << "Escolha o serviço (1=CompressPDF, 2=ConvertToTXT, 3=ConvertImageFormat, 4=ResizeImage): ";
  int choice;
  std::cin >> choice;
  std::cin.ignore();  // Limpar buffer

  std::string input_path;
  std::cout << "Caminho do arquivo: ";
  std::getline(std::cin, input_path);

  FileMetadata metadata;
  metadata.set_file_name(input_path.substr(input_path.find_last_of("/") + 1));

  std::string output_format;
  int width, height;
  if (choice == 3) {
    std::cout << "Formato de saída (ex: png): ";
    std::cin >> output_format;
    auto* params = new ConvertImageFormatParams();
    params->set_output_format(output_format);
    metadata.set_allocated_convert_image_format(params);
  } else if (choice == 4) {
    std::cout << "Largura: ";
    std::cin >> width;
    std::cout << "Altura: ";
    std::cin >> height;
    auto* params = new ResizeImageParams();
    params->set_width(width);
    params->set_height(height);
    metadata.set_allocated_resize_image(params);
  } else if (choice == 1) {
    metadata.set_allocated_compress_pdf(new CompressPDFParams());
  } else if (choice == 2) {
    metadata.set_allocated_convert_to_txt(new ConvertToTXTParams());
  } else {
    std::cerr << "Escolha inválida" << std::endl;
    return 1;
  }

  ClientContext context;
  std::unique_ptr<ClientReaderWriter<RequestChunk, ResponseChunk>> stream;

  if (choice == 1) stream = stub->CompressPDF(&context);
  else if (choice == 2) stream = stub->ConvertToTXT(&context);
  else if (choice == 3) stream = stub->ConvertImageFormat(&context);
  else if (choice == 4) stream = stub->ResizeImage(&context);

  // Enviar metadados
  RequestChunk first_chunk;
  first_chunk.set_allocated_metadata(new FileMetadata(metadata));
  stream->Write(first_chunk);

  // Enviar chunks do arquivo
  std::ifstream input_file(input_path, std::ios::binary);
  if (!input_file) {
    std::cerr << "Erro ao abrir arquivo de entrada" << std::endl;
    return 1;
  }
  char buffer[1024];
  while (input_file.read(buffer, sizeof(buffer))) {
    RequestChunk chunk;
    chunk.mutable_data()->set_content(buffer, input_file.gcount());
    stream->Write(chunk);
  }
  input_file.close();
  stream->WritesDone();

  // Receber resposta
  std::string output_path = "output_" + std::to_string(choice) + "_" + metadata.file_name();
  std::ofstream output_file(output_path, std::ios::binary);
  bool success = false;
  std::string message;
  ResponseChunk resp;
  while (stream->Read(&resp)) {
    if (resp.has_data()) {
      output_file.write(resp.data().content().c_str(), resp.data().content().size());
    } else if (resp.has_status()) {
      success = resp.status().success();
      message = resp.status().message();
    }
  }
  output_file.close();

  Status status = stream->Finish();
  if (!status.ok()) {
    std::cerr << "Erro gRPC: " << status.error_message() << std::endl;
    return 1;
  }

  if (success) {
    std::cout << "Sucesso. Salvo em: " << output_path << std::endl;
  } else {
    std::cout << "Erro: " << message << std::endl;
  }

  return 0;
}
