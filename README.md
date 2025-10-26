# grpc-file-processor2

# grpc-file-processor2 — Como rodar (Ubuntu 24.04.2 LTS, dev container)

Este README descreve os passos para compilar e executar o servidor C++ e os clientes (C++ e Python) no workspace.

Pré-requisitos (instalar no container de desenvolvimento):
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config protobuf-compiler \
  libprotobuf-dev libgrpc++-dev grpc-proto python3-pip \
  imagemagick poppler-utils ghostscript
```

1) Gerar stubs a partir do .proto
```bash
cd /workspaces/grpc-file-processor2

# C++ (server + client)
protoc -I=proto --cpp_out=server_cpp --grpc_out=server_cpp --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) proto/file_processor.proto
protoc -I=proto --cpp_out=client_cpp --grpc_out=client_cpp --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) proto/file_processor.proto

# Python (para client_python)
python3 -m pip install grpcio-tools
python3 -m grpc_tools.protoc -I=proto --python_out=client_python --grpc_python_out=client_python proto/file_processor.proto
```

2) Compilar e executar o servidor C++
```bash
cd /workspaces/grpc-file-processor2/server_cpp
cmake . && make

# Em um terminal separado:
./server
```

cd /workspaces/grpc-file-processor2/server_cpp

# iniciar em foreground (ver logs direto)
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu ./server

# ou iniciar em background e salvar log
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu ./server > server.log 2>&1 & echo $! > server.pid
tail -f server.log


3) Compilar e executar o cliente C++ (em outro terminal)
```bash
cd /workspaces/grpc-file-processor2/client_cpp

# Gerar stubs (se ainda não gerou)
protoc -I=../proto --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) ../proto/file_processor.proto

cmake . && make

# Observação: neste ambiente o cliente requer usar bibliotecas do sistema.
# Comando que funcionou no workspace:
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu ./client
```

Observações: se houver conflito com bibliotecas do Conda (/opt/conda/lib), use uma das alternativas:
```bash
# alternativa 1: LD_LIBRARY_PATH (recomendada conforme teste)
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu ./client

# alternativa 2: forçar libstdc++ do sistema
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libstdc++.so.6 ./client

# alternativa 3: desativar ambiente conda (se aplicável)
conda deactivate
./client
```

4) Executar o cliente Python (em outro terminal)
```bash
cd /workspaces/grpc-file-processor2/client_python
python3 -m pip install -r requirements.txt

# Se não foram gerados os stubs na pasta client_python:
python3 -m grpc_tools.protoc -I=../proto --python_out=. --grpc_python_out=. ../proto/file_processor.proto

python3 client.py
```

5) Opção Docker (se preferir containerizar)
- Servidor:
```bash
docker build -f docker/Dockerfile.server -t grpc-file-server docker
docker run --rm -p 50051:50051 grpc-file-server
```
- Cliente Python (exemplo):
```bash
docker build -f docker/Dockerfile.client_python -t grpc-file-client-py docker
# usar --network host no Linux se o servidor estiver no host
docker run --rm --network host grpc-file-client-py
```

Solução para o erro comum de libstdc++ (mensagem tipo "GLIBCXX_3.4.32 not found"):
- O problema é conflito entre /opt/conda/lib (libstdc++.so.6 antiga) e as libs do sistema usadas por grpc/protobuf.
- Solução testada neste workspace:
  LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu ./client

Logs e diagnóstico:
```bash
ldd ./client | grep libstdc++
strings /usr/lib/x86_64-linux-gnu/libstdc++.so.6 | grep GLIBCXX_3.4.32 || echo "símbolo não encontrado"
LD_DEBUG=libs ./client 2>&1 | grep libstdc++
```
