#include "cache.hpp"

#include <string>
#include <iostream>
#include <chrono>
#include <memory>
#include <thread>

struct Item {
	std::chrono::system_clock::time_point expire_time;
	int value;
};

typedef std::string key_type;
typedef std::shared_ptr<Item> value_type;
typedef Cache<key_type, value_type> cache_type;

int main()
{
	const int N = 100;
	cache_type cache(1024);
	std::thread pool[N];
	std::mutex iomtx;

	for (int i = 0; i < N; i++) {
		pool[i] = std::thread([&, i]() {
			auto key = std::to_string(i);
			auto now = std::chrono::system_clock::now();
			auto lifetime = std::chrono::milliseconds(i);
			auto p = value_type(new Item);
			p->value = i*i;
			p->expire_time = now + lifetime;
			cache_type::Accessor acc(cache, key);
			acc.set(std::move(p));
		});
	}
	for (int i = 0; i < N; i++) {
		pool[i].join();
	}
	for (int i = 0; i < N; i++) {
		pool[i] = std::thread([&, i]() {
			auto key = std::to_string(i);
			auto now = std::chrono::system_clock::now();
			std::this_thread::sleep_for(
				std::chrono::milliseconds(N-i));
			cache_type::Accessor acc(cache, key);
			auto result = acc.get();
			std::lock_guard<std::mutex> locker(iomtx);
			std::cout << "thread " << i << ": ";
			if (result.first != key || !result.second) {
				std::cout << "cache not found for " << key;
			} else if (result.second->expire_time <= now) {
				std::cout << "cache expired for " << key;
			} else {
				std::cout << "cache exists for " << key
					<< ", value = " << result.second->value;
			}
			std::cout << std::endl;
		});
	}
	for (int i = 0; i < N; i++) {
		pool[i].join();
	}
}
