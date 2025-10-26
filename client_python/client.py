import grpc
import file_processor_pb2
import file_processor_pb2_grpc

CHUNK_SIZE = 64 * 1024

def upload_file_chunks(file_path):
    # primeiro, enviar meta
    meta = file_processor_pb2.UploadRequest()
    meta.meta.file_name = file_path.split('/')[-1]
    yield meta
    with open(file_path, 'rb') as f:
        while True:
            chunk = f.read(CHUNK_SIZE)
            if not chunk:
                break
            req = file_processor_pb2.UploadRequest()
            req.chunk.content = chunk
            yield req

def compress_pdf(stub, input_path, output_path):
    responses = stub.CompressPDF(upload_file_chunks(input_path))
    # receber stream de DownloadResponse (chunks e possivel status final)
    with open(output_path, 'wb') as out:
        for resp in responses:
            if resp.HasField('chunk'):
                out.write(resp.chunk.content)
            elif resp.HasField('status'):
                if not resp.status.success:
                    print("Erro:", resp.status.message)
                else:
                    print("Status:", resp.status.message)
    print("Arquivo salvo em:", output_path)

def main():
    with grpc.insecure_channel('localhost:50051') as channel:
        stub = file_processor_pb2_grpc.FileProcessorServiceStub(channel)
        compress_pdf(stub, 'input.pdf', 'compressed_output.pdf')

if __name__ == "__main__":
    main()
