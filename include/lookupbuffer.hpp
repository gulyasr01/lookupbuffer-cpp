#include <chrono>
#include <optional>
#include <string>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <thread>

template <typename Key, typename Value>
class LookupBuffer {
public:
    // Insert or overwrite the value associated with key.
    void insert(const Key& key, Value value) {
		if (cancel.load(std::memory_order_acquire)) return;

		auto entry = std::make_shared<Entry>();

		{
			std::lock_guard<std::mutex> lock(map_mut);
			if (cancel.load(std::memory_order_acquire)) return;

			auto it = map.find(key);
			if (it == map.end()) {
				// key not exist
				entry->val = std::move(value);
				map[key] = entry;

			} else {
				entry = it->second;
				it->second->val = std::move(value);
			}
		}

		// release lock before nitifying
		entry->cv.notify_one();
	}

    // Wait until a value for key is available, then remove and return it.
    // If the timeout expires before the key appears, return std::nullopt.
    std::optional<Value> drop_select(
        const Key& key,
        std::chrono::milliseconds timeout) {
		if (cancel.load(std::memory_order_acquire)) return std::nullopt;

		auto deadline = std::chrono::steady_clock::now() + timeout;

		std::unique_lock<std::mutex> lock(map_mut);
		// acces the hashmap
		
		// 1. load or create the key
		auto & entry = map[key];
		if (!entry) {
			entry = std::make_shared<Entry>();
		}
		entry->waiters++;

		// 2. check if value exist, if so, take and return with it
		auto cleanup = [&]() {
			if (entry && (--entry->waiters == 0) ) {
				map.erase(key);
			}
		};

		auto take = [&]() {
			std::optional<Value> rv;
			if (entry->val.has_value()) {
				rv = std::move(entry->val.value());
				entry->val = std::nullopt;
				cleanup();
			} else {
				rv = std::nullopt;
			}

			return rv;
		};

		auto retval = take();
		if (retval) return retval;

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

		if (cancel.load(std::memory_order_acquire)) {
			cleanup();
			return std::nullopt;
		}

		retval = take();
		if (retval) return retval;

		// 4. cleanup to remove the key if nobody is waiting for it
		cleanup();

		return std::nullopt;
	}

    // Prevent further inserts and wake any waiting threads.
    void close() {
		std::lock_guard lock(map_mut);
		cancel.store(true, std::memory_order_release);

		for (const auto it : map) {
			it.second->cv.notify_all();
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
		std::condition_variable cv;
		std::size_t waiters{0};
	};

	std::unordered_map<Key, std::shared_ptr<Entry>> map;
	std::mutex map_mut;
	std::atomic_bool cancel{false};
};
