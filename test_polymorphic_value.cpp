#include "polymorphic_value.h"

#include <cassert>
#include <iostream>

struct SmallBase {
    virtual ~SmallBase() {}
    virtual void identify() { std::cout << "SmallBase" << std::endl; }
    int x = 17;
};

struct SmallSub : public SmallBase {
    SmallSub(int y) : y(y) {}
    
    void identify() { std::cout << "SmallSub " << y << std::endl; }
    int y;
};

struct BigSub : public SmallBase {
    void identify() { std::cout << "BigSub" << std::endl; }
    int y[100];
};

#if IS_STANDARDIZED
using namespace std;
#else
using namespace stdx;
#endif

static_assert(sizeof(polymorphic_value<SmallBase>) == 64 + sizeof(void*));
static_assert(sizeof(polymorphic_value < SmallBase, { .size = 2 } > ) == 2 * sizeof(void*));
static_assert(sizeof(polymorphic_value<BigSub>) == 2 * sizeof(void*));
static_assert(sizeof(polymorphic_value < BigSub, { .size = 512 } > ) == 512 + sizeof(void*));

struct MoveOnly : public SmallBase {
    MoveOnly() = default;
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) = default;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly& operator=(MoveOnly&) = default;
};


int main()
{
    polymorphic_value<SmallBase> sv;
    assert(!sv);
    sv.emplace<SmallSub>(1);
    assert(sv);
    assert(sv.has_value<SmallSub>());

    sv->identify(); // 1

    sv.value<SmallSub>().y = 2;

    std::optional<int> o0 = sv.transform<SmallSub>([](SmallSub& ss) { return ss.y + 1; });

    assert(o0 && *o0 == 3);
    
    auto sv2 = sv;
    sv2->identify();    // 2
    sv->identify();     // 2

    sv2.emplace<BigSub>();
    sv2->identify();
    sv = std::move(sv2);
    sv->identify();
    assert(!sv2);
    
    std::optional<int> o1 = sv2.transform<SmallSub>([](SmallSub& ss) { return ss.y + 1; });
    assert(!o1);
    o1 = sv.transform<SmallSub>([](SmallSub& ss) { return ss.y + 1; });
    assert(!o1);        // Still not ok as sv is a BigSub.

    SmallBase v1 = sv.value();
    SmallSub v2 = sv2.value_or<SmallSub>(SmallSub(7));
    assert(v2.y == 7);      // Default value as sv2 is empty.

    sv.emplace<SmallSub>(5);
    v2 = sv.value_or<SmallSub>(SmallSub(7));
    assert(v2.y == 5);  // Default value not used as sv contains a SmallSub
    
    // Test and_then with a function returning optional
    std::optional<int> o2 = sv.and_then<SmallSub>([](SmallSub& ss) { return std::optional(ss.y + 1); });
    assert(o2 && *o2 == 6);

    // Test and_then with a function returning polymorphic_value. This just clears the value if it contained BigSub:
    sv = sv.and_then([](SmallBase& v) {
        if (dynamic_cast<SmallSub*>(&v) == nullptr)
            return polymorphic_value<SmallBase>();
        else
            return polymorphic_value<SmallBase>(std::in_place_type<SmallSub>, static_cast<SmallSub&>(v));
    });
    assert(sv.has_value<SmallSub>());
    
    std::optional<BigSub> o3 = sv.or_else<BigSub>([]() { return std::optional<BigSub>(); });
    assert(!o3);

    // Test in_place construction
    polymorphic_value<SmallBase> sv3(std::in_place_type<SmallSub>, 4);
    sv3->identify();

    std::optional<SmallSub> o4 = sv3.or_else<SmallSub>([]() { return std::optional<SmallSub>(-1); });
    assert(o4 && o4->y == 4);

    // Test reset
    sv2.emplace<SmallSub>(5);
    assert(sv2);
    sv2.reset();
    assert(!sv2);

    auto bv = polymorphic_value<SmallBase>::make<BigSub>();
    bv->identify();

    // Move only testing
    polymorphic_value<MoveOnly> mv;
    mv.emplace<MoveOnly>();
    // auto mv2 = mv; As T is not copyable neither is polymorphic_value<T>

    // sv.emplace<MoveOnly>(); --- gives static assert failure
    try {
        sv2 = sv;       // Runtime error as T but not U is copyable.
    }
    catch (std::exception& ex) {
        std::cout << ex.what() << std::endl;
    }

    try {
        sv3.value<BigSub>();        // Fails
    }
    catch (std::exception& ex) {
        std::cout << ex.what() << std::endl;
    }

    // Test polymorphic_value_for
    polymorphic_value_for<SmallBase, SmallSub, BigSub, MoveOnly> sv4(std::in_place_type<BigSub>);

    // auto sv5 = sv4; No copy with MoveOnly in the list.
}