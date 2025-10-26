#include <grpcpp/grpcpp.h>
#include "file_processor.grpc.pb.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <cstdio>
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using file_processor::FileProcessorService;
using file_processor::UploadRequest;
using file_processor::DownloadResponse;
using file_processor::FileChunk;
using file_processor::FileMeta;
using file_processor::StatusMessage;

static std::string now_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));
    return std::string(buf);
}

void log_entry(const std::string &service, const std::string &file, const std::string &status, const std::string &message="") {
    std::ofstream log("server.log", std::ios::app);
    log << "[" << now_timestamp() << "] " << status << " - Service: " << service << ", File: " << file << ", Msg: " << message << std::endl;
}

class FileProcessorServiceImpl final : public FileProcessorService::Service {
public:
    // exemplo para CompressPDF (mesma ideia para os outros métodos)
    grpc::Status CompressPDF(ServerContext* context,
                             ServerReaderWriter<DownloadResponse, UploadRequest>* stream) override {
        std::string original_name = "unknown.pdf";
        // caminho temporário
        std::string tmp_in = "/tmp/input_" + std::to_string(std::hash<std::string>{}(now_timestamp())) + ".pdf";
        std::ofstream out(tmp_in, std::ios::binary);
        if (!out) {
            log_entry("CompressPDF", original_name, "ERROR", "Falha ao criar arquivo temporário de entrada.");
            return grpc::Status(grpc::StatusCode::INTERNAL, "Falha ao criar arquivo temporário.");
        }

        // Ler stream do cliente
        UploadRequest req;
        while (stream->Read(&req)) {
            if (req.has_meta()) {
                original_name = req.meta().file_name();
            } else if (req.has_chunk()) {
                auto bytes = req.chunk().content();
                out.write(bytes.data(), bytes.size());
            }
        }
        out.close();

        std::string tmp_out = "/tmp/output_compressed_" + std::to_string(std::hash<std::string>{}(tmp_in)) + ".pdf";
        // comando gs - sanitize paths in real code!
        std::string cmd = "gs -sDEVICE=pdfwrite -dCompatibilityLevel=1.4 -dPDFSETTINGS=/ebook -dNOPAUSE -dQUIET -dBATCH -sOutputFile=" + tmp_out + " " + tmp_in;
        int r = std::system(cmd.c_str());
        if (r != 0) {
            log_entry("CompressPDF", original_name, "ERROR", "gs failed with code " + std::to_string(r));
            // enviar status de erro (opcional via stream)
            DownloadResponse resp;
            StatusMessage sm;
            sm.set_success(false);
            sm.set_message("Falha na compressão PDF.");
            resp.mutable_status()->CopyFrom(sm);
            stream->Write(resp);
            // cleanup
            std::remove(tmp_in.c_str());
            return grpc::Status(grpc::StatusCode::INTERNAL, "Falha na compressão PDF.");
        }

        // Ler arquivo de saída e enviar em chunks
        std::ifstream fin(tmp_out, std::ios::binary);
        if (!fin) {
            log_entry("CompressPDF", original_name, "ERROR", "Falha ao abrir arquivo de saída.");
            return grpc::Status(grpc::StatusCode::INTERNAL, "Erro ao abrir arquivo de saída.");
        }
        const size_t BUF = 64 * 1024;
        char buffer[BUF];
        while (fin) {
            fin.read(buffer, BUF);
            std::streamsize s = fin.gcount();
            if (s <= 0) break;
            DownloadResponse resp;
            FileChunk *chunk = resp.mutable_chunk();
            chunk->set_content(buffer, s);
            stream->Write(resp);
        }
        fin.close();

        // Enviar status final (opcional)
        DownloadResponse final_resp;
        StatusMessage sm;
        sm.set_success(true);
        sm.set_message("Compressão concluída");
        sm.set_output_file_name("compressed_" + original_name);
        final_resp.mutable_status()->CopyFrom(sm);
        stream->Write(final_resp);

        log_entry("CompressPDF", original_name, "SUCCESS", "Compressão concluída.");

        // cleanup temporários
        std::remove(tmp_in.c_str());
        std::remove(tmp_out.c_str());

        return grpc::Status::OK;
    }

    // Implementar ConvertToTXT, ConvertImageFormat e ResizeImage com a mesma estrutura
};

int main(int argc, char** argv) {
    std::string server_address("0.0.0.0:50051");
    FileProcessorServiceImpl service;
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Servidor gRPC ouvindo em " << server_address << std::endl;
    server->Wait();
    return 0;
}
