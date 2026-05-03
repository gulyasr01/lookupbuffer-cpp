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

		std::condition_variable * cv_ptr = nullptr;

		{
			std::lock_guard<mutex> lock(map_mut);
			if (cancel.load(std::memory_order_acquire)) return;

			auto it = map.find(key);
			if (it == map.end()) {
				// key not exist
				auto entry = std::make_shared<Entry>();
				entry->val = std::move(value);
				map[key] = std::move(entry);

			} else {
				it->second->val = move(value);
				cv_ptr = &it->second->cv;
			}
		}

		// release lock before nitifying
		if (cv_ptr) cv_ptr->notify_all();
	}

    // Wait until a value for key is available, then remove and return it.
    // If the timeout expires before the key appears, return std::nullopt.
    std::optional<Value> drop_select(
        const Key& key,
        std::chrono::milliseconds timeout) {
		if (cancel.load(std::memory_order_acquire)) return std::nullopt;

		auto deadline = std::chrono::steady_clock::now() + timeout;

		std::shared_ptr<Entry> entry;

		auto cleanup = [&]() {
			if (entry && (--entry->waiters == 0) ) {
				map.erase(key);
			}
		};

		std::unique_lock<mutex> lock(map_mut);
		// acces the hashmap
		
		// 1. load or create the key
		entry = map[key];
		if (!entry) {
			entry = std::make_shared<Entry>();
			entry->waiters++;
		}

		// 2. check if value exist, if so, take and return with it
		if (entry->val.has_value()) {
			auto retval = std::move(entry->val.value());
			entry->val = std::nullopt;
			map.erase(key);
			return retval;
		}

		// 3. wait for the value to be present
		entry->cv.wait_until(lock, deadline, [&] {
			if (cancel.load(std::memory_order_acquire)) 
			{
				return true;
			}

			if (entry->val.has_value()) {
				return true;
			} else {
				return false;
			}
		});

		if (cancel.load(std::memory_order_acquire)) return std::nullopt;

		// todo: same cade as in 2, make it a lamda
		if (entry->val.has_value()) {
			auto retval = std::move(entry->val.value());
			entry->val = std::nullopt;
			map.erase(key);
			return retval;
		}

		// 4. optional: cleanup to remove the key if nobody is waiting for it
		cleanup();

		return std::nullopt;
	}

    // Prevent further inserts and wake any waiting threads.
    void close() {
		{
			std::lock_guard lock(map_mut);
			cancel.store(true, std::memory_order_release);
		}

		for (const auto it : map) {
			it->second->cv.notify_all();
		}
	}

    // Returns true if close() has been called.
    bool closed() const {
		return cancel.load(std::memory_order_acquire);
	}

private:
	struct Entry
	{
		std::optional<Value> val;
		condition_variable cv;
		std::size_t waiters{0};
	};

	std::optional<Entry> take(const Key& key) {
		auto it = map.find(key);
		if (it != map.end()) {
			// get and return the value
			auto node = map.extract(it);
			return move(node.mapped());
		}
		return std::nullopt;
	}

	unordered_map<Key, std::shared_ptr<Entry>> map;
	mutex map_mut;
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