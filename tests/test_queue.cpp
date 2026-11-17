// GoogleTest typed tests. Add a Tag to QueueTypes and a new queue variant gets the whole suite.
//
// Stress size comes from env SPSC_STRESS_N (reduced for the TSan build).
#include "ref/boost_queue.hpp"
#include "ref/mutex_queue.hpp"
#include "spsc/spsc_queue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

// Queue<T> is a class template and can't be a typed-test parameter directly -> wrap it in a Tag.
// kNeedsDefaultCtor: true if the implementation requires default-constructible T (that test is skipped).
struct SpscTag {
    template <typename T>
    using type = spsc::Queue<T>;
    static constexpr bool kNeedsDefaultCtor = false;
};
struct MutexTag {
    template <typename T>
    using type = spsc::compare::MutexQueue<T>;
    static constexpr bool kNeedsDefaultCtor = false;
};
struct BoostTag {
    template <typename T>
    using type = spsc::compare::BoostQueue<T>;
    static constexpr bool kNeedsDefaultCtor = true;
};

template <typename Tag, typename T>
using QueueOf = typename Tag::template type<T>;

class TagNames {
public:
    template <typename Tag>
    static std::string GetName(int) {
        if constexpr (std::is_same_v<Tag, SpscTag>) return "Spsc";
        if constexpr (std::is_same_v<Tag, MutexTag>) return "Mutex";
        if constexpr (std::is_same_v<Tag, BoostTag>) return "Boost";
        return "Unknown";
    }
};

using QueueTypes = ::testing::Types<SpscTag, MutexTag, BoostTag>;

std::uint64_t stress_n(std::uint64_t def) {
    const char* s = std::getenv("SPSC_STRESS_N");
    return s ? std::strtoull(s, nullptr, 10) : def;
}

// Counts live instances (constructions minus destructions).
struct Counted {
    static inline int alive = 0;
    int v;
    Counted() : v(-1) { ++alive; }
    explicit Counted(int x) : v(x) { ++alive; }
    Counted(const Counted& o) : v(o.v) { ++alive; }
    Counted(Counted&& o) noexcept : v(o.v) { ++alive; }
    Counted& operator=(const Counted&) = default;
    Counted& operator=(Counted&&) noexcept = default;
    ~Counted() { --alive; }
};

template <typename Tag>
class QueueTest : public ::testing::Test {};
TYPED_TEST_SUITE(QueueTest, QueueTypes, TagNames);

// ---------------- single-threaded ----------------

TYPED_TEST(QueueTest, CapacityRoundsUpToPowerOfTwo) {
    EXPECT_EQ((QueueOf<TypeParam, int>(1).capacity()), 1u);
    EXPECT_EQ((QueueOf<TypeParam, int>(3).capacity()), 4u);
    EXPECT_EQ((QueueOf<TypeParam, int>(8).capacity()), 8u);
    EXPECT_EQ((QueueOf<TypeParam, int>(1000).capacity()), 1024u);
    EXPECT_THROW((QueueOf<TypeParam, int>(0)), std::invalid_argument);
}

TYPED_TEST(QueueTest, FillAndDrain) {
    QueueOf<TypeParam, int> q(4);
    int out = -1;
    EXPECT_FALSE(q.try_pop(out)) << "new queue must be empty";
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.try_push(i));
    EXPECT_FALSE(q.try_push(99)) << "full queue must reject push";
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(q.try_pop(out));
        EXPECT_EQ(out, i);
    }
    EXPECT_FALSE(q.try_pop(out));
}

TYPED_TEST(QueueTest, Wraparound) {
    QueueOf<TypeParam, int> q(4);
    int next_push = 0, next_pop = 0, out;
    // Push/pop in varying batch sizes so indices wrap around many times.
    for (int round = 0; round < 1000; ++round) {
        const int k = round % 4 + 1;
        for (int i = 0; i < k; ++i) ASSERT_TRUE(q.try_push(next_push++));
        for (int i = 0; i < k; ++i) {
            ASSERT_TRUE(q.try_pop(out));
            ASSERT_EQ(out, next_pop++);
        }
    }
    EXPECT_FALSE(q.try_pop(out)) << "queue must be empty after draining";
}

TYPED_TEST(QueueTest, CapacityOne) {
    QueueOf<TypeParam, int> q(1);
    int out;
    EXPECT_TRUE(q.try_push(7));
    EXPECT_FALSE(q.try_push(8));
    ASSERT_TRUE(q.try_pop(out));
    EXPECT_EQ(out, 7);
    EXPECT_FALSE(q.try_pop(out));
}

TYPED_TEST(QueueTest, OptionalPopAndEmplace) {
    QueueOf<TypeParam, std::string> q(8);
    EXPECT_TRUE(q.try_push(std::string(100, 'x')));  // long enough to defeat SSO
    EXPECT_TRUE(q.try_emplace(3, 'y'));
    auto a = q.try_pop();
    auto b = q.try_pop();
    auto c = q.try_pop();
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(*a, std::string(100, 'x'));
    EXPECT_EQ(*b, "yyy");
    EXPECT_FALSE(c);
}

TYPED_TEST(QueueTest, MoveOnlyType) {
    QueueOf<TypeParam, std::unique_ptr<int>> q(2);
    EXPECT_TRUE(q.try_push(std::make_unique<int>(42)));
    auto p = q.try_pop();
    ASSERT_TRUE(p && *p);
    EXPECT_EQ(**p, 42);
}

TYPED_TEST(QueueTest, DestroysRemainingElements) {
    Counted::alive = 0;
    {
        QueueOf<TypeParam, Counted> q(8);
        for (int i = 0; i < 5; ++i) ASSERT_TRUE(q.try_emplace(i));
        EXPECT_EQ(Counted::alive, 5);
        auto x = q.try_pop();
        ASSERT_TRUE(x);
        EXPECT_EQ(x->v, 0);
        EXPECT_EQ(Counted::alive, 5);  // 4 in the queue + x
    }
    EXPECT_EQ(Counted::alive, 0) << "queue destructor must destroy remaining elements";
}

// Type without a default constructor. Only implementations that don't pre-construct empty slots can hold it.
struct NoDefault {
    int v;
    explicit NoDefault(int x) : v(x) {}
};

TYPED_TEST(QueueTest, NonDefaultConstructibleType) {
    if constexpr (TypeParam::kNeedsDefaultCtor) {
        GTEST_SKIP() << "implementation requires default-constructible T";
    } else {
        QueueOf<TypeParam, NoDefault> q(4);
        EXPECT_TRUE(q.try_emplace(5));
        auto x = q.try_pop();
        ASSERT_TRUE(x);
        EXPECT_EQ(x->v, 5);
    }
}

// ---------------- two-thread stress ----------------
// Assertions run only on the consumer (= the test's main thread).

template <typename Q, typename Make>
void run_ordered_stress(std::uint64_t n, std::size_t cap, Make make) {
    Q q(cap);
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < n; ++i) {
            auto v = make(i);
            while (!q.try_push(std::move(v))) {
            }
        }
    });
    typename std::decay_t<decltype(make(0))> v{};
    std::uint64_t i = 0;
    for (; i < n; ++i) {
        while (!q.try_pop(v)) {
        }
        if (v != make(i)) break;
    }
    producer.join();
    EXPECT_EQ(i, n) << "mismatch at item " << i;
}

TYPED_TEST(QueueTest, StressOrderedInts) {
    const std::uint64_t n = stress_n(2'000'000);
    for (std::size_t cap : {1, 2, 16, 1024}) {
        SCOPED_TRACE("capacity=" + std::to_string(cap));
        run_ordered_stress<QueueOf<TypeParam, std::uint64_t>>(
            n, cap, [](std::uint64_t i) { return i; });
    }
}

// Heap-allocating type: slot construct/destroy ordering bugs are easy for TSan to catch.
TYPED_TEST(QueueTest, StressOrderedStrings) {
    const std::uint64_t n = stress_n(2'000'000) / 10;
    for (std::size_t cap : {1, 2, 16, 1024}) {
        SCOPED_TRACE("capacity=" + std::to_string(cap));
        run_ordered_stress<QueueOf<TypeParam, std::string>>(n, cap, [](std::uint64_t i) {
            return "item-" + std::to_string(i) + std::string(32, '.');
        });
    }
}

}  // namespace
