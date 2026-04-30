#include <iostream>
#include <chrono>
#include <optional>
#include <string>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <thread>

using namespace std;

template <typename Key, typename Value>
class LookupBuffer {
public:
    // Insert or overwrite the value associated with key.
    void insert(const Key& key, Value value) {
		if (cancel.load(std::memory_order_acquire)) return;

		{
			std::lock_guard<mutex> lock(map_mut);
			if (cancel.load(std::memory_order_acquire)) return;

			map[key] = move(value);
		}
		
		map_cv.notify_all();
	}

    // Wait until a value for key is available, then remove and return it.
    // If the timeout expires before the key appears, return std::nullopt.
    std::optional<Value> drop_select(
        const Key& key,
        std::chrono::milliseconds timeout) {
			if (cancel.load(std::memory_order_acquire)) return std::nullopt;

			auto deadline = std::chrono::steady_clock::now() + timeout;

			std::unique_lock<mutex> lock(map_mut);
			// acces the hashmap
			auto opt_val = take(key);
			if (opt_val.has_value()) {
				return opt_val.value();
			}

			bool woke = map_cv.wait_until(lock, deadline, [&] {
				if (cancel.load(std::memory_order_acquire)) return true;

				auto it = map.find(key);
				if (it == map.end()) return false;
				else return true;
			});

			if (cancel.load(std::memory_order_acquire)) return std::nullopt;

			if (woke == false) return std::nullopt;

			return take(key);
		}

    // Prevent further inserts and wake any waiting threads.
    void close() {
		{
			std::lock_guard lock(map_mut);
			cancel.store(true, std::memory_order_release);
		}

		map_cv.notify_all();
	}

    // Returns true if close() has been called.
    bool closed() const {
		return cancel.load(std::memory_order_acquire);
	}

private:
	std::optional<Value> take(const Key& key) {
		auto it = map.find(key);
		if (it != map.end()) {
			// get and return the value
			auto node = map.extract(it);
			return move(node.mapped());
		}
		return std::nullopt;
	}

	unordered_map<Key, Value> map;
	mutex map_mut;
	condition_variable map_cv;
	std::atomic_bool cancel{false};
};


int main() {

	LookupBuffer<int, int> buff{};

	jthread([&]{
		buff.insert(1, 2);
	});
	
	jthread([&] {
		sleep(1);
		auto rb = buff.drop_select(1, 500ms);
		if (rb.has_value()) {
			cout << "val 1: " << rb.value() << endl;	
		}
	});


	auto rb = buff.drop_select(5, 500ms);
	cout << "val 5: " << rb.has_value() << endl;
}