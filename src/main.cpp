#include <iostream>

#include "database.h"
#include "db.h"
#include "threadpool.h"
int multiply(int a, int b, int c) { return a * b + c; }

// int test_thread() {
//     noia::thread_pool pool{};
//     auto result1 = pool.enqueue(multiply, 3, 4);
//     auto result2 = pool.enqueue(multiply, 5, 6);
//     auto result3 = pool.enqueue(multiply, 7, 8);
//     std::cout << result1.get() << std::endl;
//     std::cout << result2.get() << std::endl;
//     std::cout << result3.get() << std::endl;
// }

int main() { std::cout << "In Main Function" << std::endl; 
    init_system();

    // test_thread();
}
