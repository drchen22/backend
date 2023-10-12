#pragma once

#include <netinet/in.h>
#include <iostream>
#include "headers.h"
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>


namespace noia {
    namespace network {
        class connection {
          public:
            connection(const std::string& port);

            void handle_url(std::string& path);

          private:
            int socketFd;
            struct sockaddr_in address_;
            pqxx::work worker_;
        };
    }  // namespace network
}  // namespace noia
