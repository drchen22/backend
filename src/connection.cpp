#include "connection.h"

namespace noia {

    namespace network {

        connection::connection(const std::string& port) {
            int socketFd = socket(AF_INET, SOCK_STREAM, 0);

            address_.sin_family = AF_INET;
            address_.sin_addr.s_addr = INADDR_ANY;
            address_.sin_port = htons(8765);
            bind(socketFd, (struct sockaddr*)&address_, sizeof(address_));
        }

    
}  // namespace noia::network

}  // namespace noia