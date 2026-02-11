#include <async/gen.hpp>
#include <print>
// Task foo() {
//     std::cout << "Hello from coroutine!" << std::endl;
//     co_return;
// }
// Generator<int> counter(int start, int end) {
//     while (start < end) {
//         co_yield start;
//         ++start;
//     }
// }

int main() {
    std::println("hello world");
}

// using FSM = async_generator<std::string, std::byte>;
// static const std::byte ESC{'H'};
// static const std::byte SOF{0x10};

// FSM Parse() {
//     while (true) {
//         std::byte b = co_await std::byte{};
//         if(ESC != b) {
//             continue;
//         }
//         b = co_await std::byte{};
//         if(SOF != b) {
//             continue;
//         }
//         std::string frame{};
//         while(true) {
//             b = co_await std::byte{};
//             if(ESC == b) {
//                 b = co_await std::byte{};
//                 if(SOF == b) {
//                     co_yield frame;
//                     break;
//                 }
//             }
//             frame += static_cast<char>(b);
//         }
//     }
// }
// void ProcessStream(Generator<std::byte>& stream, FSM& parse) {
//     for(const auto& b : stream) {
//         parse.SendSignal(b);

//         if(const auto& res = parse(); res.length()) {
//             std::println("{}",res);
//         }
//     }
// }

// constexpr std::byte operator""_B(unsigned long long value) noexcept {
//     return static_cast<std::byte>(value);
// }

// constexpr std::byte operator""_B(char c) noexcept {
//     return static_cast<std::byte>(c);
// }

// Task<int, false> foo() {
//     co_return 42;
// }

// Task<void, false> foo2() {
//     std::println("foo2 start");
//     co_return ;
// }

// Task<int> bar() {
//     std::println("bar start...");
//     std::println("waiting foo");
//     auto result = co_await foo();
//     std::println("waiting foo2");
//     co_await foo2();
//     std::println("get from foo: {}", result);
//     std::println("bar end...");
//     co_return 0;
// }

// int main() {
//     LOG::i("hello world");
//     LOG::d("hello world");
//     LOG::e("hello world");
//     LOG::w("hello world");

//     // auto g = counter(1, 10);
//     // for (auto i : g) {
//     //     std::println("{}",i);
//     // }

//     // std::vector<std::byte> fakeBytes1{
//     //     0x70_B, ESC, SOF, ESC,'H'_B, 'e'_B, 'l'_B, 'l'_B, 'o'_B, ESC, SOF,0x7_B, ESC, SOF};

//     // auto stream1 = sender(fakeBytes1);
//     // auto parser = Parse();
//     // ProcessStream(stream1, parser);

//     auto fooTask = bar();
//     fooTask.resume();
//     fooTask.resume();
//     // std::println("{}", fooTask());
//     return 0;
// }

// Generator<int>::from(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)
//     .filter([](auto i) {
//         std::cout << "filter: " << i << std::endl;
//         return i % 2 == 0;
//     })
//     .map([](auto i) {
//         std::cout << "map: " << i << std::endl;
//         return i * 3;
//     })
//     .flat_map([](auto i) -> Generator<int> {
//         std::cout << "flat_map: " << i << std::endl;
//         for (int j = 0; j < i; ++j) {
//             co_yield j;
//         }
//     })
//     .take(3)
//     .for_each([](auto i) { std::cout << "for_each: " << i << std::endl; });

// fibonacci().take_while([](auto i) { return i < 100; }).for_each([](auto i) {
//     std::cout << i << " ";
// });
// std::cout << std::endl;

// auto seq = fibonacci().map([](auto i) { return std::to_string(i); });

// for (int i = 0; i < 10; i++) {
//     std::cout << seq.next() << std::endl;
// }
