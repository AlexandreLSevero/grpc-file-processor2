import grpc
import file_processor_pb2
import file_processor_pb2_grpc
import sys

def send_request(stub, service, input_path, params=None):
  def request_iterator():
    metadata = file_processor_pb2.FileMetadata(file_name=input_path.split('/')[-1])
    if service == 'CompressPDF':
      metadata.compress_pdf.CopyFrom(file_processor_pb2.CompressPDFParams())
    elif service == 'ConvertToTXT':
      metadata.convert_to_txt.CopyFrom(file_processor_pb2.ConvertToTXTParams())
    elif service == 'ConvertImageFormat':
      cif_params = file_processor_pb2.ConvertImageFormatParams(output_format=params['output_format'])
      metadata.convert_image_format.CopyFrom(cif_params)
    elif service == 'ResizeImage':
      ri_params = file_processor_pb2.ResizeImageParams(width=params['width'], height=params['height'])
      metadata.resize_image.CopyFrom(ri_params)
    yield file_processor_pb2.RequestChunk(metadata=metadata)

    with open(input_path, 'rb') as f:
      while True:
        chunk = f.read(1024)
        if not chunk:
          break
        yield file_processor_pb2.RequestChunk(data=file_processor_pb2.FileChunk(content=chunk))

  if service == 'CompressPDF':
    response_stream = stub.CompressPDF(request_iterator())
  elif service == 'ConvertToTXT':
    response_stream = stub.ConvertToTXT(request_iterator())
  elif service == 'ConvertImageFormat':
    response_stream = stub.ConvertImageFormat(request_iterator())
  elif service == 'ResizeImage':
    response_stream = stub.ResizeImage(request_iterator())

  success = False
  message = ""
  output_path = "output_" + service.lower() + "_" + input_path.split('/')[-1]
  with open(output_path, 'wb') as out_file:
    for resp in response_stream:
      if resp.HasField('data'):
        out_file.write(resp.data.content)
      elif resp.HasField('status'):
        success = resp.status.success
        message = resp.status.message

  if success:
    print(f"{service} concluído. Salvo em: {output_path}")
  else:
    print(f"Erro em {service}: {message}")

def main():
  with grpc.insecure_channel('localhost:50051') as channel:
    stub = file_processor_pb2_grpc.FileProcessorServiceStub(channel)

    print("Escolha o serviço:")
    print("1: CompressPDF")
    print("2: ConvertToTXT")
    print("3: ConvertImageFormat")
    print("4: ResizeImage")
    choice = input("Número: ")
    input_path = input("Caminho do arquivo: ")

    params = {}
    if choice == '3':
      params['output_format'] = input("Formato de saída (ex: png): ")
    elif choice == '4':
      params['width'] = int(input("Largura: "))
      params['height'] = int(input("Altura: "))

    services = {'1': 'CompressPDF', '2': 'ConvertToTXT', '3': 'ConvertImageFormat', '4': 'ResizeImage'}
    service = services.get(choice)
    if service:
      send_request(stub, service, input_path, params)
    else:
      print("Escolha inválida")

if __name__ == '__main__':
  main()
