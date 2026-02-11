/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <utils/GenerationalArena.h>

#include <gtest/gtest.h>

using namespace utils;

namespace {

struct Foo {
    int x;
    int y;
    Foo(int x, int y) : x(x), y(y) {}
};

struct DtorCounter {
    static int count;
    int id;
    DtorCounter(int id) : id(id) {}
    ~DtorCounter() { count++; }
};
int DtorCounter::count = 0;

} // namespace

TEST(GenerationalArenaTest, AllocationAndGet) {
    GenerationalArena<Foo> arena(10);
    
    auto p1 = arena.allocate(1, 2);
    EXPECT_TRUE(p1);
    EXPECT_NE(p1.generation, 0u);
    
    Foo* f1 = arena.get(p1);
    ASSERT_NE(f1, nullptr);
    EXPECT_EQ(f1->x, 1);
    EXPECT_EQ(f1->y, 2);

    auto p2 = arena.allocate(3, 4);
    EXPECT_TRUE(p2);
    EXPECT_NE(p1, p2);
    
    Foo* f2 = arena.get(p2);
    ASSERT_NE(f2, nullptr);
    EXPECT_EQ(f2->x, 3);
    EXPECT_EQ(f2->y, 4);
    
    // Check const get
    const GenerationalArena<Foo>& constArena = arena;
    const Foo* cf1 = constArena.get(p1);
    ASSERT_NE(cf1, nullptr);
    EXPECT_EQ(cf1->x, 1);
}

TEST(GenerationalArenaTest, FreeAndReuse) {
    GenerationalArena<Foo> arena(2);
    
    auto p1 = arena.allocate(10, 20);
    auto p2 = arena.allocate(30, 40);
    
    // Arena is full
    // auto p3 = arena.allocate(50, 60); // This would assert/crash in FixedCapacityVector
    
    arena.free(p1);
    EXPECT_EQ(arena.get(p1), nullptr);
    EXPECT_NE(arena.get(p2), nullptr);
    
    // Reuse slot 1
    auto p3 = arena.allocate(50, 60);
    EXPECT_TRUE(p3);
    // Index should match p1 (0)
    EXPECT_EQ(p3.index, p1.index);
    // Generation should be different
    EXPECT_NE(p3.generation, p1.generation);
    
    Foo* f3 = arena.get(p3);
    ASSERT_NE(f3, nullptr);
    EXPECT_EQ(f3->x, 50);
    
    // Old handle should not work
    EXPECT_EQ(arena.get(p1), nullptr);
}

TEST(GenerationalArenaTest, DoubleFreeProtection) {
    GenerationalArena<Foo> arena(10);
    auto p1 = arena.allocate(1, 2);
    
    arena.free(p1);
    // Double free should be safe (no-op)
    arena.free(p1);
    
    // Allocate should still work correctly
    auto p2 = arena.allocate(3, 4);
    EXPECT_TRUE(p2);
}

TEST(GenerationalArenaTest, DestructorCalls) {
    DtorCounter::count = 0;
    {
        GenerationalArena<DtorCounter> arena(10);
        auto p1 = arena.allocate(1);
        auto p2 = arena.allocate(2);
        auto p3 = arena.allocate(3);
        
        arena.free(p2); // Destroys object 2. count becomes 1.
        EXPECT_EQ(DtorCounter::count, 1);
    } // arena destruction. Should destroy p1 and p3. count becomes 1 + 2 = 3.
    
    EXPECT_EQ(DtorCounter::count, 3);
}

TEST(GenerationalArenaTest, InvalidAccess) {
    GenerationalArena<Foo> arena(10);
    auto p1 = arena.allocate(1, 2);
    
    GenerationalArena<Foo>::Pointer invalidP = { p1.generation, p1.index + 100 };
    EXPECT_EQ(arena.get(invalidP), nullptr);
    
    GenerationalArena<Foo>::Pointer invalidGen = { p1.generation + 1, p1.index };
    EXPECT_EQ(arena.get(invalidGen), nullptr);
}
