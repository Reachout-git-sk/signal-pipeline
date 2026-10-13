#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "bounded_queue.h"

using namespace std::chrono_literals;

TEST(BoundedQueue, FifoOrderSingleThread) {
    sp::BoundedQueue<int> q(4);
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(q.push(i));
    for (int i = 0; i < 4; ++i) EXPECT_EQ(q.pop().value(), i);
}

TEST(BoundedQueue, MoveOnlyItems) {
    sp::BoundedQueue<std::unique_ptr<int>> q(2);
    q.push(std::make_unique<int>(7));
    auto p = q.pop();
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(**p, 7);
}

TEST(BoundedQueue, PushBlocksWhenFull) {
    sp::BoundedQueue<int> q(1);
    q.push(1);
    std::atomic<bool> pushed{false};
    std::thread t([&] {
        q.push(2);
        pushed = true;
    });
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(pushed.load());  // still waiting for space
    EXPECT_EQ(q.pop().value(), 1);
    t.join();
    EXPECT_TRUE(pushed.load());
    EXPECT_EQ(q.pop().value(), 2);
}

TEST(BoundedQueue, CloseWakesBlockedConsumersAndDrains) {
    sp::BoundedQueue<int> q(4);
    q.push(10);
    q.close();
    EXPECT_FALSE(q.push(11));
    EXPECT_EQ(q.pop().value(), 10);  // items already queued are still delivered
    EXPECT_FALSE(q.pop().has_value());

    sp::BoundedQueue<int> empty(4);
    std::thread waiter([&] { EXPECT_FALSE(empty.pop().has_value()); });
    std::this_thread::sleep_for(20ms);
    empty.close();
    waiter.join();
}

TEST(BoundedQueue, CloseWakesBlockedProducer) {
    sp::BoundedQueue<int> q(1);
    q.push(1);
    std::thread t([&] { EXPECT_FALSE(q.push(2)); });
    std::this_thread::sleep_for(20ms);
    q.close();
    t.join();
}

// Many producers and consumers on a small queue: every item must come out
// exactly once. Small capacity forces lots of blocking on both sides.
TEST(BoundedQueue, NoLossOrDuplicationUnderContention) {
    constexpr int producers = 8, consumers = 8, per_producer = 20000;
    sp::BoundedQueue<int> q(16);
    std::vector<std::vector<int>> got(consumers);

    std::vector<std::thread> threads;
    for (int c = 0; c < consumers; ++c)
        threads.emplace_back([&, c] {
            while (auto v = q.pop()) got[c].push_back(*v);
        });
    std::vector<std::thread> prod;
    for (int p = 0; p < producers; ++p)
        prod.emplace_back([&, p] {
            for (int i = 0; i < per_producer; ++i) ASSERT_TRUE(q.push(p * per_producer + i));
        });
    for (auto& t : prod) t.join();
    q.close();
    for (auto& t : threads) t.join();

    std::vector<int> all;
    for (auto& g : got) all.insert(all.end(), g.begin(), g.end());
    ASSERT_EQ(all.size(), static_cast<std::size_t>(producers * per_producer));
    std::sort(all.begin(), all.end());
    for (int i = 0; i < producers * per_producer; ++i) ASSERT_EQ(all[i], i);
    EXPECT_EQ(q.total_pushes(), static_cast<std::size_t>(producers * per_producer));
}

// Each producer's items must stay in order relative to each other.
TEST(BoundedQueue, PerProducerOrderPreserved) {
    sp::BoundedQueue<std::pair<int, int>> q(8);
    std::thread a([&] { for (int i = 0; i < 5000; ++i) q.push({0, i}); });
    std::thread b([&] { for (int i = 0; i < 5000; ++i) q.push({1, i}); });
    int last[2] = {-1, -1};
    for (int n = 0; n < 10000; ++n) {
        auto [who, seq] = q.pop().value();
        ASSERT_GT(seq, last[who]);
        last[who] = seq;
    }
    a.join();
    b.join();
}
