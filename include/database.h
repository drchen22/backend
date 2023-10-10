#pragma once

#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string> 
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
void init_system();
int start_webserver(pqxx::work& worker);

void handle_request(std::string path, pqxx::work& worker, int clientSocket);