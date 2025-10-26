#include <grpcpp/grpcpp.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <ctime>
#include <sys/stat.h>
#include "file_processor.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::Status;
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

class FileProcessorServiceImpl final : public FileProcessorService::Service {
private:
  void Log(const std::string& level, const std::string& service_name, const std::string& file_name, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;
    localtime_r(&now_c, &now_tm);
    char timestamp[26];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &now_tm);

    std::ofstream log_file("server.log", std::ios::app);
    if (log_file.is_open()) {
      log_file << "[" << timestamp << "] " << level << " - Service: " << service_name
               << ", File: " << file_name << ", Message: " << message << std::endl;
      log_file.close();
    } else {
      std::cerr << "Falha ao abrir arquivo de log!" << std::endl;
    }
    std::cout << "[" << timestamp << "] " << level << " - Service: " << service_name
              << ", File: " << file_name << ", Message: " << message << std::endl;
  }

  void LogSuccess(const std::string& service_name, const std::string& file_name, const std::string& message) {
    Log("SUCCESS", service_name, file_name, message);
  }

  void LogError(const std::string& service_name, const std::string& file_name, const std::string& message) {
    Log("ERROR", service_name, file_name, message);
  }

  Status SendError(ServerWriter<ResponseChunk>* writer, const std::string& msg) {
    ResponseChunk err;
    auto* status = err.mutable_status();
    status->set_success(false);
    status->set_message(msg);
    writer->Write(err);
    return Status(grpc::StatusCode::INTERNAL, msg);
  }

  Status ProcessAndSend(const std::string& service_name, const std::string& input_path,
                        const std::string& output_path, const std::string& cmd,
                        ServerWriter<ResponseChunk>* writer, const std::string& file_name,
                        const std::string& success_msg) {
    int result = system(cmd.c_str());
    if (result != 0) {
      LogError(service_name, file_name, "Falha no processamento. Código: " + std::to_string(result));
      return SendError(writer, "Falha no processamento");
    }
    LogSuccess(service_name, file_name, success_msg);

    std::ifstream output_file(output_path, std::ios::binary);
    if (!output_file) {
      LogError(service_name, file_name, "Falha ao abrir output");
      return SendError(writer, "Erro ao abrir output");
    }
    char buffer[1024];
    while (output_file.read(buffer, sizeof(buffer))) {
      ResponseChunk resp_chunk;
      resp_chunk.mutable_data()->set_content(buffer, output_file.gcount());
      writer->Write(resp_chunk);
    }
    output_file.close();

    ResponseChunk status_chunk;
    auto* status = status_chunk.mutable_status();
    status->set_success(true);
    status->set_message("Sucesso");
    writer->Write(status_chunk);

    std::remove(input_path.c_str());
    std::remove(output_path.c_str());
    return Status::OK;
  }

  Status ReceiveFile(ServerReader<RequestChunk>* reader, std::string* file_name,
                     FileMetadata* metadata_out) {
    RequestChunk first_chunk;
    if (!reader->Read(&first_chunk) || !first_chunk.has_metadata()) {
      return Status(grpc::StatusCode::INVALID_ARGUMENT, "Metadados ausentes");
    }
    *metadata_out = first_chunk.metadata();
    *file_name = metadata_out->file_name();
    return Status::OK;
  }

  Status SaveInputStream(ServerReader<RequestChunk>* reader, const std::string& input_path) {
    std::ofstream input_file(input_path, std::ios::binary);
    if (!input_file) {
      return Status(grpc::StatusCode::INTERNAL, "Falha ao criar input");
    }
    RequestChunk chunk;
    while (reader->Read(&chunk)) {
      if (chunk.has_data()) {
        input_file.write(chunk.data().content().c_str(), chunk.data().content().size());
      }
    }
    input_file.close();
    return Status::OK;
  }

public:
  Status CompressPDF(ServerContext* context, ServerReader<RequestChunk>* reader,
                     ServerWriter<ResponseChunk>* writer) override {
    FileMetadata metadata;
    std::string file_name;
    auto status = ReceiveFile(reader, &file_name, &metadata);
    if (!status.ok()) return status;

    std::string input_path = "/tmp/input_" + file_name;
    std::string output_path = "/tmp/output_compressed.pdf";

    status = SaveInputStream(reader, input_path);
    if (!status.ok()) return status;

    std::string cmd = "gs -sDEVICE=pdfwrite -dCompatibilityLevel=1.4 -dPDFSETTINGS=/ebook "
                      "-dNOPAUSE -dQUIET -dBATCH -sOutputFile=\"" + output_path + "\" \"" + input_path + "\"";
    return ProcessAndSend("CompressPDF", input_path, output_path, cmd, writer, file_name, "Compressão bem-sucedida");
  }

  Status ConvertToTXT(ServerContext* context, ServerReader<RequestChunk>* reader,
                      ServerWriter<ResponseChunk>* writer) override {
    FileMetadata metadata;
    std::string file_name;
    auto status = ReceiveFile(reader, &file_name, &metadata);
    if (!status.ok()) return status;

    std::string input_path = "/tmp/input_" + file_name;
    std::string output_path = "/tmp/output.txt";

    status = SaveInputStream(reader, input_path);
    if (!status.ok()) return status;

    std::string cmd = "pdftotext \"" + input_path + "\" \"" + output_path + "\"";
    return ProcessAndSend("ConvertToTXT", input_path, output_path, cmd, writer, file_name, "Conversão para TXT bem-sucedida");
  }

  Status ConvertImageFormat(ServerContext* context, ServerReader<RequestChunk>* reader,
                            ServerWriter<ResponseChunk>* writer) override {
    FileMetadata metadata;
    std::string file_name;
    auto status = ReceiveFile(reader, &file_name, &metadata);
    if (!status.ok()) return status;

    if (!metadata.has_convert_image_format()) {
      return SendError(writer, "Parâmetros de conversão ausentes");
    }
    std::string output_format = metadata.convert_image_format().output_format();
    std::string input_path = "/tmp/input_" + file_name;
    std::string output_path = "/tmp/output." + output_format;

    status = SaveInputStream(reader, input_path);
    if (!status.ok()) return status;

    std::string cmd = "convert \"" + input_path + "\" \"" + output_path + "\"";
    return ProcessAndSend("ConvertImageFormat", input_path, output_path, cmd, writer, file_name, "Conversão de formato bem-sucedida");
  }

  Status ResizeImage(ServerContext* context, ServerReader<RequestChunk>* reader,
                     ServerWriter<ResponseChunk>* writer) override {
    FileMetadata metadata;
    std::string file_name;
    auto status = ReceiveFile(reader, &file_name, &metadata);
    if (!status.ok()) return status;

    if (!metadata.has_resize_image()) {
      return SendError(writer, "Parâmetros de resize ausentes");
    }
    int width = metadata.resize_image().width();
    int height = metadata.resize_image().height();
    std::string input_path = "/tmp/input_" + file_name;
    std::string output_ext = file_name.substr(file_name.find_last_of(".") + 1);
    std::string output_path = "/tmp/output_resized." + output_ext;

    status = SaveInputStream(reader, input_path);
    if (!status.ok()) return status;

    std::string cmd = "convert \"" + input_path + "\" -resize " + std::to_string(width) + "x" + std::to_string(height) +
                      " \"" + output_path + "\"";
    return ProcessAndSend("ResizeImage", input_path, output_path, cmd, writer, file_name, "Redimensionamento bem-sucedido");
  }
};
