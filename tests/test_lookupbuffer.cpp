#include <gtest/gtest.h>

#include "lookupbuffer.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

TEST(LookupBufferTest, InsertBeforeDropReturnsValue) {
    LookupBuffer<int, std::string> buffer;

    buffer.insert(1, "hello");

    auto result = buffer.drop_select(1, 10ms);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "hello");
}

TEST(LookupBufferTest, DropRemovesValue) {
    LookupBuffer<int, std::string> buffer;

    buffer.insert(1, "hello");

    auto first = buffer.drop_select(1, 10ms);
    auto second = buffer.drop_select(1, 10ms);

    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, "hello");

    EXPECT_FALSE(second.has_value());
}

TEST(LookupBufferTest, InsertOverwritesExistingValue) {
    LookupBuffer<int, std::string> buffer;

    buffer.insert(1, "old");
    buffer.insert(1, "new");

    auto result = buffer.drop_select(1, 10ms);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "new");
}

TEST(LookupBufferTest, DropWaitsForFutureInsert) {
    LookupBuffer<int, std::string> buffer;

    auto future = std::async(std::launch::async, [&] {
        return buffer.drop_select(42, 2s);
    });

    std::this_thread::sleep_for(50ms);

    buffer.insert(42, "value");

    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);

    auto result = future.get();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value");
}

TEST(LookupBufferTest, DropTimesOutWhenKeyIsMissing) {
    LookupBuffer<int, std::string> buffer;

    auto result = buffer.drop_select(999, 50ms);

    EXPECT_FALSE(result.has_value());
}

TEST(LookupBufferTest, InsertForDifferentKeyDoesNotSatisfyWaiter) {
    LookupBuffer<int, std::string> buffer;

    auto future = std::async(std::launch::async, [&] {
        return buffer.drop_select(1, 250ms);
    });

    std::this_thread::sleep_for(50ms);

    buffer.insert(2, "wrong-key");

    EXPECT_EQ(future.wait_for(100ms), std::future_status::timeout);

    buffer.insert(1, "right-key");

    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);

    auto result = future.get();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "right-key");
}

TEST(LookupBufferTest, CloseWakesWaitingDropper) {
    LookupBuffer<int, std::string> buffer;

    auto future = std::async(std::launch::async, [&] {
        return buffer.drop_select(1, 10s);
    });

    std::this_thread::sleep_for(50ms);

    buffer.close();

    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);

    auto result = future.get();

    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(buffer.closed());
}

TEST(LookupBufferTest, InsertAfterCloseDoesNothing) {
    LookupBuffer<int, std::string> buffer;

    buffer.close();
    buffer.insert(1, "value");

    auto result = buffer.drop_select(1, 10ms);

    EXPECT_FALSE(result.has_value());
}

TEST(LookupBufferTest, DropAfterCloseReturnsImmediately) {
    LookupBuffer<int, std::string> buffer;

    buffer.close();

    auto start = std::chrono::steady_clock::now();
    auto result = buffer.drop_select(1, 10s);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.has_value());
    EXPECT_LT(elapsed, 100ms);
}

TEST(LookupBufferTest, MultipleWaitersSameKeyOnlyOneReceivesSingleInsertedValue) {
    LookupBuffer<int, std::string> buffer;

    auto f1 = std::async(std::launch::async, [&] {
        return buffer.drop_select(1, 500ms);
    });

    auto f2 = std::async(std::launch::async, [&] {
        return buffer.drop_select(1, 500ms);
    });

    std::this_thread::sleep_for(50ms);

    buffer.insert(1, "one-value");

    ASSERT_EQ(f1.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(f2.wait_for(1s), std::future_status::ready);

    auto r1 = f1.get();
    auto r2 = f2.get();

    int received_count = 0;

    if (r1.has_value()) {
        EXPECT_EQ(*r1, "one-value");
        ++received_count;
    }

    if (r2.has_value()) {
        EXPECT_EQ(*r2, "one-value");
        ++received_count;
    }

    EXPECT_EQ(received_count, 1);
}

TEST(LookupBufferTest, MultipleInsertsCanSatisfyMultipleWaitersForSameKey) {
    LookupBuffer<int, std::string> buffer;

    auto f1 = std::async(std::launch::async, [&] {
        return buffer.drop_select(1, 1s);
    });

    auto f2 = std::async(std::launch::async, [&] {
        return buffer.drop_select(1, 1s);
    });

    std::this_thread::sleep_for(50ms);

    buffer.insert(1, "first");
    std::this_thread::sleep_for(50ms);
    buffer.insert(1, "second");

    ASSERT_EQ(f1.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(f2.wait_for(1s), std::future_status::ready);

    auto r1 = f1.get();
    auto r2 = f2.get();

    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    EXPECT_NE(*r1, *r2);

    EXPECT_TRUE(
        (*r1 == "first" && *r2 == "second") ||
        (*r1 == "second" && *r2 == "first")
    );
}

TEST(LookupBufferTest, CloseWakesMultipleWaitersOnDifferentKeys) {
    LookupBuffer<int, std::string> buffer;

    constexpr int waiter_count = 16;

    std::vector<std::future<std::optional<std::string>>> futures;
    futures.reserve(waiter_count);

    for (int i = 0; i < waiter_count; ++i) {
        futures.push_back(std::async(std::launch::async, [&, i] {
            return buffer.drop_select(i, 10s);
        }));
    }

    std::this_thread::sleep_for(100ms);

    buffer.close();

    for (auto& future : futures) {
        ASSERT_EQ(future.wait_for(1s), std::future_status::ready);

        auto result = future.get();
        EXPECT_FALSE(result.has_value());
    }
}

TEST(LookupBufferTest, SupportsMoveOnlyValues) {
    LookupBuffer<int, std::unique_ptr<int>> buffer;

    buffer.insert(1, std::make_unique<int>(123));

    auto result = buffer.drop_select(1, 10ms);

    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result->get(), nullptr);
    EXPECT_EQ(**result, 123);
}

TEST(LookupBufferTest, ManyDistinctKeysCanBeDroppedConcurrently) {
    LookupBuffer<int, int> buffer;

    constexpr int count = 128;

    std::vector<std::future<std::optional<int>>> futures;
    futures.reserve(count);

    for (int i = 0; i < count; ++i) {
        futures.push_back(std::async(std::launch::async, [&, i] {
            return buffer.drop_select(i, 2s);
        }));
    }

    std::this_thread::sleep_for(100ms);

    for (int i = 0; i < count; ++i) {
        buffer.insert(i, i * 10);
    }

    for (int i = 0; i < count; ++i) {
        ASSERT_EQ(futures[i].wait_for(1s), std::future_status::ready);

        auto result = futures[i].get();

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, i * 10);
    }
}
