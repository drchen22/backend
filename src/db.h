#include <cstddef>
#include <iostream>
#include <pqxx/pqxx>
#include <random>
#include <nlohmann/json.hpp>

int db_test() {
  std::cout << "HELLO WORLD\n";

  std::string conn_str = "user=jinchen password=Jc2003@! hostaddr=47.100.38.198 port=5433 dbname=db1eaf95783b3a4b9abaca0f1213a574c1test_db";
  pqxx::connection mydb{conn_str.c_str()};

  std::cout << "Connected to " << mydb.dbname() << '\n';

  pqxx::work worker{mydb};

  pqxx::result response = worker.exec("SELECT * FROM products");

  nlohmann::json result;

  for (size_t i = 0; i < response.size(); i++) {
    std::cout << "Id: " << response[i][0] << " TEST: " << response[i][1] << "\n";
    result[i] = {{"id", response[i][0].c_str()}, {"name", response[i][1].c_str()}};
    
  }
  std::cout << result;



  return 1;
}
