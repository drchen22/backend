#include "database.h"
#include "threadpool.h"

static std::string conn_str = "user=chen password=JC2003@! hostaddr=110.42.163.188 port=5432 dbname=backend";

void init_system() {
  pqxx::connection mydb{conn_str.c_str()};
  std::cout << "Connected to " << mydb.dbname() << '\n';
  pqxx::work worker{mydb};


  start_webserver(worker);
}



int start_webserver(pqxx::work& worker) {

    noia::thread_pool pool{};

  int socketFd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY; 
  address.sin_port = htons(8765);
  bind(socketFd, (struct sockaddr *)&address, sizeof(address));

  listen(socketFd, 5);

  while(true) {
    sockaddr_in cli_addr;
    int sin_len = sizeof(cli_addr);
    int clientSocket = accept(socketFd, (sockaddr*)&cli_addr, (socklen_t*)&sin_len); 
    //std::cout << "已接收请求" << std::endl;
    // Receive request
    char request[1024];
    recv(clientSocket, request, 1024, 0);
    // std::cout << request << std::endl; 

    // 处理请求
    std::stringstream req_stream(request);

    std::string request_method;
    std::string request_path;
    std::string request_protocol;

    std::getline(req_stream, request_method, ' ');
    std::getline(req_stream, request_path, ' ');

    std::cout << request_path << std::endl;


    // 处理API


    handle_request(request_path, worker, clientSocket);

    

    close(clientSocket);
  }
}

void handle_request(std::string path, pqxx::work& worker, int clientSocket) {
  if(path == "/product") {
      pqxx::result data = worker.exec("SELECT * FROM product");
      nlohmann::json result;
      for (size_t i = 0; i < data.size(); i++) {
        result[i] = {{"id", data[i][0].c_str()},
                     {"name", data[i][1].c_str()},
                     {"description", data[i][2].c_str()},
                     {"price", data[i][3].c_str()},
                     {"image_link", data[i][4].c_str()},
                    };
        // std::cout << data[i][4].c_str() << std::endl;
      }

      std::string result_str = result.dump();
      std::cout << result_str << std::endl;

      std::string response = "HTTP/1.1 200 OK\n";
      response += "Content-Type: application/json; charset=UTF-8\n\n";
      response += result_str;
      send(clientSocket, response.c_str(), response.size(), 0);

      return;
  }
    std::string response = "HTTP/1.1 api\r\n\r\n Welcome!";
    send(clientSocket, response.c_str(), response.size(), 0);
    return;
}
