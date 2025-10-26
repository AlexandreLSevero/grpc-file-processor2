#include "file_processor_service_impl.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>

namespace {

static std::string MakeTempPath(const std::string& prefix, const std::string& filename) {
  auto ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
  auto pos = filename.find_last_of("/\\");
  std::string base = (pos == std::string::npos) ? filename : filename.substr(pos + 1);
  if (base.empty()) base = "file";
  return std::string("/tmp/") + prefix + "_" + ts + "_" + base;
}

static bool FileExistsAndNotEmpty(const std::string& path) {
  struct stat st;
  return (stat(path.c_str(), &st) == 0) && (st.st_size > 0);
}

static void RemoveIfExists(const std::string& path) {
  if (!path.empty()) ::remove(path.c_str());
}

} // namespace

// CompressPDF (unchanged)
grpc::Status FileProcessorServiceImpl::CompressPDF(
    grpc::ServerContext* /*context*/,
    grpc::ServerReaderWriter<file_processor::ResponseChunk, file_processor::RequestChunk>* stream) {
  file_processor::RequestChunk req;
  file_processor::FileMetadata metadata;

  if (stream->Read(&req) && req.has_metadata()) {
    metadata = req.metadata();
  }

  const std::string in_path = MakeTempPath("in", metadata.file_name());
  const std::string out_path = in_path + ".compressed.pdf";
  const std::string gs_log = in_path + ".gs.log";

  {
    std::ofstream ofs(in_path, std::ios::binary);
    if (!ofs) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("CompressPDF: falha ao criar arquivo temporário de entrada");
      stream->Write(status_chunk);
      return grpc::Status::OK;
    }
    while (stream->Read(&req)) {
      if (req.has_data()) {
        const std::string& c = req.data().content();
        ofs.write(c.data(), static_cast<std::streamsize>(c.size()));
      }
    }
    ofs.close();
  }

  {
    std::ifstream probe(in_path, std::ios::binary);
    if (!probe) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("CompressPDF: não foi possível abrir o arquivo recebido");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      return grpc::Status::OK;
    }
    char hdr[5] = {0};
    probe.read(hdr, 4);
    probe.close();
    if (std::string(hdr, 4) != "%PDF") {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("CompressPDF: arquivo recebido não parece ser PDF");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      return grpc::Status::OK;
    }
  }

  std::string gs_cmd = "gs -sDEVICE=pdfwrite -dCompatibilityLevel=1.4 -dPDFSETTINGS=/ebook -dNOPAUSE -dQUIET -dBATCH -sOutputFile=\"" + out_path + "\" \"" + in_path + "\" 2>\"" + gs_log + "\"";
  int ret = std::system(gs_cmd.c_str());

  bool ok = (ret == 0) && FileExistsAndNotEmpty(out_path);
  if (!ok) {
    std::string log_first_line;
    std::ifstream lg(gs_log);
    if (lg) {
      std::getline(lg, log_first_line);
      lg.close();
    }
    file_processor::ResponseChunk status_chunk;
    status_chunk.mutable_status()->set_success(false);
    std::string msg = "CompressPDF: Ghostscript falhou";
    if (!log_first_line.empty()) msg += ": " + log_first_line;
    status_chunk.mutable_status()->set_message(msg);
    stream->Write(status_chunk);
    RemoveIfExists(in_path);
    RemoveIfExists(out_path);
    RemoveIfExists(gs_log);
    return grpc::Status::OK;
  }

  {
    std::ifstream ifs(out_path, std::ios::binary);
    if (!ifs) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("CompressPDF: não foi possível abrir o arquivo comprimido");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      RemoveIfExists(out_path);
      RemoveIfExists(gs_log);
      return grpc::Status::OK;
    }

    const size_t CHUNK = 64 * 1024;
    std::vector<char> buf(CHUNK);
    while (ifs) {
      ifs.read(buf.data(), static_cast<std::streamsize>(buf.size()));
      std::streamsize n = ifs.gcount();
      if (n <= 0) break;
      file_processor::ResponseChunk data_chunk;
      data_chunk.mutable_data()->set_content(buf.data(), static_cast<size_t>(n));
      stream->Write(data_chunk);
    }
    ifs.close();
  }

  file_processor::ResponseChunk done;
  done.mutable_status()->set_success(true);
  done.mutable_status()->set_message("CompressPDF concluído");
  stream->Write(done);

  RemoveIfExists(in_path);
  RemoveIfExists(out_path);
  RemoveIfExists(gs_log);
  return grpc::Status::OK;
}

// ConvertToTXT (unchanged)
grpc::Status FileProcessorServiceImpl::ConvertToTXT(
    grpc::ServerContext* /*context*/,
    grpc::ServerReaderWriter<file_processor::ResponseChunk, file_processor::RequestChunk>* stream) {
  file_processor::RequestChunk req;
  file_processor::FileMetadata metadata;

  if (stream->Read(&req) && req.has_metadata()) {
    metadata = req.metadata();
  }

  const std::string in_path = MakeTempPath("in", metadata.file_name());
  const std::string out_path = in_path + ".txt";
  const std::string log_path = in_path + ".log";

  size_t total_bytes_written = 0;
  {
    std::ofstream ofs(in_path, std::ios::binary);
    if (!ofs) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ConvertToTXT: falha ao criar arquivo temporário de entrada");
      stream->Write(status_chunk);
      return grpc::Status::OK;
    }
    while (stream->Read(&req)) {
      if (req.has_data()) {
        const std::string& c = req.data().content();
        ofs.write(c.data(), static_cast<std::streamsize>(c.size()));
        total_bytes_written += c.size();
      }
    }
    ofs.flush();
    ofs.close();
  }

  if (!FileExistsAndNotEmpty(in_path)) {
    file_processor::ResponseChunk status_chunk;
    status_chunk.mutable_status()->set_success(false);
    status_chunk.mutable_status()->set_message("ConvertToTXT: arquivo de entrada vazio ou não criado");
    stream->Write(status_chunk);
    RemoveIfExists(in_path);
    return grpc::Status::OK;
  }

  {
    std::ifstream probe(in_path, std::ios::binary);
    if (!probe) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ConvertToTXT: não foi possível abrir o arquivo recebido");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      return grpc::Status::OK;
    }
    char hdr[5] = {0};
    probe.read(hdr, 4);
    probe.close();
    if (std::string(hdr, 4) != "%PDF") {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ConvertToTXT: arquivo recebido não parece ser PDF");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      return grpc::Status::OK;
    }
  }

  {
    std::string test_cmd = "pdftotext -l 1 \"" + in_path + "\" /dev/null 2>\"" + log_path + "\"";
    int ret = std::system(test_cmd.c_str());
    if (ret != 0) {
      std::string log_content;
      std::ifstream lg(log_path);
      if (lg) {
        std::string line;
        while (std::getline(lg, line) && log_content.length() < 200) {
          log_content += line + "; ";
        }
        lg.close();
      }
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      std::string msg = "ConvertToTXT: falha ao processar PDF";
      if (!log_content.empty()) msg += ": " + log_content;
      else msg += ": possível PDF protegido por senha ou corrompido";
      msg += " (tamanho do arquivo de entrada: " + std::to_string(total_bytes_written) + " bytes)";
      status_chunk.mutable_status()->set_message(msg);
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      RemoveIfExists(log_path);
      return grpc::Status::OK;
    }
  }

  std::string cmd = "pdftotext \"" + in_path + "\" \"" + out_path + "\" 2>\"" + log_path + "\"";
  int ret = std::system(cmd.c_str());

  bool ok = (ret == 0) && FileExistsAndNotEmpty(out_path);
  if (!ok) {
    std::string log_content;
    std::ifstream lg(log_path);
    if (lg) {
      std::string line;
      while (std::getline(lg, line) && log_content.length() < 200) {
        log_content += line + "; ";
      }
      lg.close();
    }
    file_processor::ResponseChunk status_chunk;
    status_chunk.mutable_status()->set_success(false);
    std::string msg = "ConvertToTXT: pdftotext falhou";
    if (!log_content.empty()) msg += ": " + log_content;
    else if (ret == 0) msg = "ConvertToTXT: arquivo de saída vazio (PDF pode conter apenas imagens, requer OCR)";
    msg += " (tamanho do arquivo de entrada: " + std::to_string(total_bytes_written) + " bytes)";
    status_chunk.mutable_status()->set_message(msg);
    stream->Write(status_chunk);
    RemoveIfExists(in_path);
    RemoveIfExists(out_path);
    RemoveIfExists(log_path);
    return grpc::Status::OK;
  }

  {
    std::ifstream ifs(out_path, std::ios::binary);
    if (!ifs) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ConvertToTXT: não foi possível abrir o arquivo TXT convertido");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      RemoveIfExists(out_path);
      RemoveIfExists(log_path);
      return grpc::Status::OK;
    }

    file_processor::ResponseChunk metadata_chunk;
    metadata_chunk.mutable_metadata()->set_file_name(metadata.file_name() + ".txt");
    if (!stream->Write(metadata_chunk)) {
      RemoveIfExists(in_path);
      RemoveIfExists(out_path);
      RemoveIfExists(log_path);
      return grpc::Status(grpc::StatusCode::INTERNAL, "Falha ao enviar metadados");
    }

    const size_t CHUNK = 64 * 1024;
    std::vector<char> buf(CHUNK);
    while (ifs) {
      ifs.read(buf.data(), static_cast<std::streamsize>(buf.size()));
      std::streamsize n = ifs.gcount();
      if (n <= 0) break;
      file_processor::ResponseChunk data_chunk;
      data_chunk.mutable_data()->set_content(buf.data(), static_cast<size_t>(n));
      if (!stream->Write(data_chunk)) {
        RemoveIfExists(in_path);
        RemoveIfExists(out_path);
        RemoveIfExists(log_path);
        return grpc::Status(grpc::StatusCode::INTERNAL, "Falha ao enviar dados convertidos");
      }
    }
    ifs.close();
  }

  file_processor::ResponseChunk done;
  done.mutable_status()->set_success(true);
  done.mutable_status()->set_message("ConvertToTXT concluído (tamanho do arquivo TXT: " + std::to_string(FileExistsAndNotEmpty(out_path) ? std::filesystem::file_size(out_path) : 0) + " bytes)");
  stream->Write(done);

  RemoveIfExists(in_path);
  RemoveIfExists(out_path);
  RemoveIfExists(log_path);

  return grpc::Status::OK;
}

// ConvertImageFormat (modified)
grpc::Status FileProcessorServiceImpl::ConvertImageFormat(
    grpc::ServerContext* /*context*/,
    grpc::ServerReaderWriter<file_processor::ResponseChunk, file_processor::RequestChunk>* stream) {
  file_processor::RequestChunk req;
  file_processor::FileMetadata metadata;

  // Ler metadados iniciais (se enviado)
  if (stream->Read(&req) && req.has_metadata()) {
    metadata = req.metadata();
  }

  const std::string in_path = MakeTempPath("in", metadata.file_name());
  std::string out_ext = "png";
  if (!metadata.convert_image_format().output_format().empty()) {
    out_ext = metadata.convert_image_format().output_format();
  }
  const std::string out_path = in_path + "." + out_ext;
  const std::string log_path = in_path + ".log";

  // Gravar conteúdo recebido em arquivo
  size_t total_bytes_written = 0;
  {
    std::ofstream ofs(in_path, std::ios::binary);
    if (!ofs) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ConvertImageFormat: falha ao criar arquivo temporário de entrada");
      stream->Write(status_chunk);
      return grpc::Status::OK;
    }
    while (stream->Read(&req)) {
      if (req.has_data()) {
        const std::string& c = req.data().content();
        ofs.write(c.data(), static_cast<std::streamsize>(c.size()));
        total_bytes_written += c.size();
      }
    }
    ofs.flush(); // Garantir que todos os dados sejam escritos no disco
    ofs.close();
  }

  // Verificar se o arquivo de entrada contém dados
  if (!FileExistsAndNotEmpty(in_path)) {
    file_processor::ResponseChunk status_chunk;
    status_chunk.mutable_status()->set_success(false);
    status_chunk.mutable_status()->set_message("ConvertImageFormat: arquivo de entrada vazio ou não criado");
    stream->Write(status_chunk);
    RemoveIfExists(in_path);
    return grpc::Status::OK;
  }

  // Verificar magic header da imagem (JPEG, PNG, GIF)
  {
    std::ifstream probe(in_path, std::ios::binary);
    if (!probe) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ConvertImageFormat: não foi possível abrir o arquivo recebido");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      return grpc::Status::OK;
    }
    char hdr[8] = {0};
    probe.read(hdr, 8);
    std::streamsize bytes_read = probe.gcount();
    probe.close();
    std::string header(hdr, bytes_read);
    bool is_image = false;
    // JPEG: FF D8 FF
    if (bytes_read >= 3 && header[0] == (char)0xFF && header[1] == (char)0xD8 && header[2] == (char)0xFF) {
      is_image = true;
    }
    // PNG: 89 50 4E 47 0D 0A 1A 0A
    else if (bytes_read >= 8 && header == "\x89PNG\r\n\x1A\n") {
      is_image = true;
    }
    // GIF: GIF87a or GIF89a
    else if (bytes_read >= 6 && (header.substr(0, 6) == "GIF87a" || header.substr(0, 6) == "GIF89a")) {
      is_image = true;
    }
    if (!is_image) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ConvertImageFormat: arquivo recebido não parece ser uma imagem válida (JPEG, PNG ou GIF)");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      return grpc::Status::OK;
    }
  }

  // Executar ImageMagick convert com log de erro
  std::string cmd = "convert \"" + in_path + "\" \"" + out_path + "\" 2>\"" + log_path + "\"";
  int ret = std::system(cmd.c_str());

  bool ok = (ret == 0) && FileExistsAndNotEmpty(out_path);
  if (!ok) {
    std::string log_content;
    std::ifstream lg(log_path);
    if (lg) {
      std::string line;
      while (std::getline(lg, line) && log_content.length() < 200) {
        log_content += line + "; ";
      }
      lg.close();
    }
    file_processor::ResponseChunk status_chunk;
    status_chunk.mutable_status()->set_success(false);
    std::string msg = "ConvertImageFormat: imagemagick convert falhou";
    if (!log_content.empty()) msg += ": " + log_content;
    msg += " (tamanho do arquivo de entrada: " + std::to_string(total_bytes_written) + " bytes)";
    status_chunk.mutable_status()->set_message(msg);
    stream->Write(status_chunk);
    RemoveIfExists(in_path);
    RemoveIfExists(out_path);
    RemoveIfExists(log_path);
    return grpc::Status::OK;
  }

  // Enviar arquivo convertido em chunks
  {
    std::ifstream ifs(out_path, std::ios::binary);
    if (!ifs) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ConvertImageFormat: não foi possível abrir o arquivo convertido");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      RemoveIfExists(out_path);
      RemoveIfExists(log_path);
      return grpc::Status::OK;
    }

    // Enviar metadados do arquivo convertido
    file_processor::ResponseChunk metadata_chunk;
    metadata_chunk.mutable_metadata()->set_file_name(metadata.file_name() + "." + out_ext);
    if (!stream->Write(metadata_chunk)) {
      RemoveIfExists(in_path);
      RemoveIfExists(out_path);
      RemoveIfExists(log_path);
      return grpc::Status(grpc::StatusCode::INTERNAL, "Falha ao enviar metadados");
    }

    const size_t CHUNK = 64 * 1024;
    std::vector<char> buf(CHUNK);
    while (ifs) {
      ifs.read(buf.data(), static_cast<std::streamsize>(buf.size()));
      std::streamsize n = ifs.gcount();
      if (n <= 0) break;
      file_processor::ResponseChunk data_chunk;
      data_chunk.mutable_data()->set_content(buf.data(), static_cast<size_t>(n));
      if (!stream->Write(data_chunk)) {
        RemoveIfExists(in_path);
        RemoveIfExists(out_path);
        RemoveIfExists(log_path);
        return grpc::Status(grpc::StatusCode::INTERNAL, "Falha ao enviar dados convertidos");
      }
    }
    ifs.close();
  }

  // Enviar status final
  file_processor::ResponseChunk done;
  done.mutable_status()->set_success(true);
  done.mutable_status()->set_message("ConvertImageFormat concluído (tamanho do arquivo convertido: " + std::to_string(FileExistsAndNotEmpty(out_path) ? std::filesystem::file_size(out_path) : 0) + " bytes)");
  stream->Write(done);

  // Limpar arquivos temporários
  RemoveIfExists(in_path);
  RemoveIfExists(out_path);
  RemoveIfExists(log_path);
  return grpc::Status::OK;
}

// ResizeImage (modified)
grpc::Status FileProcessorServiceImpl::ResizeImage(
    grpc::ServerContext* /*context*/,
    grpc::ServerReaderWriter<file_processor::ResponseChunk, file_processor::RequestChunk>* stream) {
  file_processor::RequestChunk req;
  file_processor::FileMetadata metadata;

  // Ler metadados iniciais (se enviado)
  if (stream->Read(&req) && req.has_metadata()) {
    metadata = req.metadata();
  }

  const std::string in_path = MakeTempPath("in", metadata.file_name());
  const std::string log_path = in_path + ".log";

  // Determinar extensão de saída
  std::string out_ext;
  auto pos = metadata.file_name().find_last_of('.');
  if (pos != std::string::npos) {
    out_ext = metadata.file_name().substr(pos + 1);
  } else {
    out_ext = "png";
  }
  const std::string out_path = in_path + "." + out_ext;

  // Gravar conteúdo recebido em arquivo
  size_t total_bytes_written = 0;
  {
    std::ofstream ofs(in_path, std::ios::binary);
    if (!ofs) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ResizeImage: falha ao criar arquivo temporário de entrada");
      stream->Write(status_chunk);
      return grpc::Status::OK;
    }
    while (stream->Read(&req)) {
      if (req.has_data()) {
        const std::string& c = req.data().content();
        ofs.write(c.data(), static_cast<std::streamsize>(c.size()));
        total_bytes_written += c.size();
      }
    }
    ofs.flush(); // Garantir que todos os dados sejam escritos no disco
    ofs.close();
  }

  // Verificar se o arquivo de entrada contém dados
  if (!FileExistsAndNotEmpty(in_path)) {
    file_processor::ResponseChunk status_chunk;
    status_chunk.mutable_status()->set_success(false);
    status_chunk.mutable_status()->set_message("ResizeImage: arquivo de entrada vazio ou não criado");
    stream->Write(status_chunk);
    RemoveIfExists(in_path);
    return grpc::Status::OK;
  }

  // Verificar magic header da imagem (JPEG, PNG, GIF)
  {
    std::ifstream probe(in_path, std::ios::binary);
    if (!probe) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ResizeImage: não foi possível abrir o arquivo recebido");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      return grpc::Status::OK;
    }
    char hdr[8] = {0};
    probe.read(hdr, 8);
    std::streamsize bytes_read = probe.gcount();
    probe.close();
    std::string header(hdr, bytes_read);
    bool is_image = false;
    // JPEG: FF D8 FF
    if (bytes_read >= 3 && header[0] == (char)0xFF && header[1] == (char)0xD8 && header[2] == (char)0xFF) {
      is_image = true;
    }
    // PNG: 89 50 4E 47 0D 0A 1A 0A
    else if (bytes_read >= 8 && header == "\x89PNG\r\n\x1A\n") {
      is_image = true;
    }
    // GIF: GIF87a or GIF89a
    else if (bytes_read >= 6 && (header.substr(0, 6) == "GIF87a" || header.substr(0, 6) == "GIF89a")) {
      is_image = true;
    }
    if (!is_image) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ResizeImage: arquivo recebido não parece ser uma imagem válida (JPEG, PNG ou GIF)");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      return grpc::Status::OK;
    }
  }

  // Configurar dimensões de redimensionamento
  int width = static_cast<int>(metadata.resize_image().width());
  int height = static_cast<int>(metadata.resize_image().height());
  std::string size_arg;
  if (width > 0 && height > 0) {
    size_arg = std::to_string(width) + "x" + std::to_string(height);
  } else if (width > 0) {
    size_arg = std::to_string(width);
  } else if (height > 0) {
    size_arg = "x" + std::to_string(height);
  } else {
    size_arg = "800x600";
  }

  // Executar ImageMagick convert com log de erro
  std::string cmd = "convert \"" + in_path + "\" -resize " + size_arg + " \"" + out_path + "\" 2>\"" + log_path + "\"";
  int ret = std::system(cmd.c_str());

  bool ok = (ret == 0) && FileExistsAndNotEmpty(out_path);
  if (!ok) {
    std::string log_content;
    std::ifstream lg(log_path);
    if (lg) {
      std::string line;
      while (std::getline(lg, line) && log_content.length() < 200) {
        log_content += line + "; ";
      }
      lg.close();
    }
    file_processor::ResponseChunk status_chunk;
    status_chunk.mutable_status()->set_success(false);
    std::string msg = "ResizeImage: imagemagick convert falhou";
    if (!log_content.empty()) msg += ": " + log_content;
    msg += " (tamanho do arquivo de entrada: " + std::to_string(total_bytes_written) + " bytes)";
    status_chunk.mutable_status()->set_message(msg);
    stream->Write(status_chunk);
    RemoveIfExists(in_path);
    RemoveIfExists(out_path);
    RemoveIfExists(log_path);
    return grpc::Status::OK;
  }

  // Enviar arquivo redimensionado em chunks
  {
    std::ifstream ifs(out_path, std::ios::binary);
    if (!ifs) {
      file_processor::ResponseChunk status_chunk;
      status_chunk.mutable_status()->set_success(false);
      status_chunk.mutable_status()->set_message("ResizeImage: não foi possível abrir o arquivo redimensionado");
      stream->Write(status_chunk);
      RemoveIfExists(in_path);
      RemoveIfExists(out_path);
      RemoveIfExists(log_path);
      return grpc::Status::OK;
    }

    // Enviar metadados do arquivo redimensionado
    file_processor::ResponseChunk metadata_chunk;
    metadata_chunk.mutable_metadata()->set_file_name(metadata.file_name());
    if (!stream->Write(metadata_chunk)) {
      RemoveIfExists(in_path);
      RemoveIfExists(out_path);
      RemoveIfExists(log_path);
      return grpc::Status(grpc::StatusCode::INTERNAL, "Falha ao enviar metadados");
    }

    const size_t CHUNK = 64 * 1024;
    std::vector<char> buf(CHUNK);
    while (ifs) {
      ifs.read(buf.data(), static_cast<std::streamsize>(buf.size()));
      std::streamsize n = ifs.gcount();
      if (n <= 0) break;
      file_processor::ResponseChunk data_chunk;
      data_chunk.mutable_data()->set_content(buf.data(), static_cast<size_t>(n));
      if (!stream->Write(data_chunk)) {
        RemoveIfExists(in_path);
        RemoveIfExists(out_path);
        RemoveIfExists(log_path);
        return grpc::Status(grpc::StatusCode::INTERNAL, "Falha ao enviar dados redimensionados");
      }
    }
    ifs.close();
  }

  // Enviar status final
  file_processor::ResponseChunk done;
  done.mutable_status()->set_success(true);
  done.mutable_status()->set_message("ResizeImage concluído (tamanho do arquivo redimensionado: " + std::to_string(FileExistsAndNotEmpty(out_path) ? std::filesystem::file_size(out_path) : 0) + " bytes)");
  stream->Write(done);

  // Limpar arquivos temporários
  RemoveIfExists(in_path);
  RemoveIfExists(out_path);
  RemoveIfExists(log_path);
  return grpc::Status::OK;
}
