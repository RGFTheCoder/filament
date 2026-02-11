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

#ifndef TNT_UTILS_GENERATIONAL_ARENA_H
#define TNT_UTILS_GENERATIONAL_ARENA_H

#include <utils/FixedCapacityVector.h>
#include <utils/compiler.h>

#include <assert.h>
#include <new>
#include <stdint.h>
#include <utility>

namespace utils {

/**
 * A container that provides generational handles to objects.
 *
 * Objects are allocated from a fixed-capacity pool. Each allocation returns a Pointer
 * (handle) that contains an index and a generation ID. When an object is freed, its
 * slot is reused and the generation ID is incremented. This allows Pointers to be
 * validated to ensure they still refer to the original object.
 *
 * @tparam T The type of object to store.
 */
template <typename T>
class GenerationalArena {
public:
    struct Pointer {
        uint32_t generation;
        uint32_t index;

        bool operator==(const Pointer& other) const {
            return generation == other.generation && index == other.index;
        }
        bool operator!=(const Pointer& other) const {
            return !(*this == other);
        }
        explicit operator bool() const {
            return generation != 0;
        }
    };

    /**
     * Creates an arena with the specified capacity.
     * The arena initially contains no objects.
     */
    explicit GenerationalArena(size_t capacity) : mData(FixedCapacityVector<Entry>::with_capacity(capacity)) {}

    /**
     * Destroys the arena and all contained objects.
     */
    ~GenerationalArena() {
        for (auto& entry : mData) {
            if (!(entry.generation & FREE_FLAG)) {
                entry.value.~T();
            }
        }
    }

    GenerationalArena(const GenerationalArena&) = delete;
    GenerationalArena& operator=(const GenerationalArena&) = delete;

    /**
     * Allocates a new object in the arena, constructing it with the given arguments.
     * Returns a handle to the object.
     */
    template <typename... Args>
    Pointer allocate(Args&&... args) {
        uint32_t index;
        if (mFreeHead != kInvalidIndex) {
            index = mFreeHead;
            Entry& entry = mData[index];
            mFreeHead = entry.nextFree;
            
            // Construct T. If this throws, the entry remains marked FREE (from before).
            new (&entry.value) T(std::forward<Args>(args)...);

            // Mark as allocated by clearing the FREE_FLAG.
            entry.generation &= GENERATION_MASK;
            return { entry.generation, index };
        }

        // Check if we have space.
        // FixedCapacityVector will assert if we exceed capacity, but we can check here too.
        assert(mData.size() < mData.capacity());

        index = static_cast<uint32_t>(mData.size());
        
        // Push a new entry. Generation defaults to FREE_FLAG.
        mData.emplace_back();
        Entry& entry = mData.back();
        
        // Construct T. If this throws, entry.generation is still FREE_FLAG.
        new (&entry.value) T(std::forward<Args>(args)...);

        // Set initial generation to 1.
        entry.generation = 1;
        return { entry.generation, index };
    }

    /**
     * Frees the object referred to by the given pointer.
     * The generation of the slot is incremented.
     * The pointer must be valid (i.e. get(p) would return non-null).
     */
    void free(Pointer p) {
        if (UTILS_UNLIKELY(p.index >= mData.size())) {
            return;
        }

        Entry& entry = mData[p.index];
        // Check if generation matches.
        if (entry.generation != p.generation) {
            return;
        }
        
        // Double check it's not already free (although generation check usually covers this 
        // unless p.generation also has the flag).
        if (entry.generation & FREE_FLAG) {
            return;
        }

        // Destroy the object.
        entry.value.~T();

        // Increment generation and mark as free.
        // We use the lower bits for generation count.
        uint32_t gen = (entry.generation + 1) & GENERATION_MASK;
        if (gen == 0) gen = 1; // avoid 0 if we treat it as special, though here it just wraps.
        
        entry.generation = gen | FREE_FLAG;
        entry.nextFree = mFreeHead;
        mFreeHead = p.index;
    }

    /**
     * Returns a pointer to the object if the handle is valid, otherwise nullptr.
     */
    T* get(Pointer p) {
        if (UTILS_UNLIKELY(p.index >= mData.size())) {
            return nullptr;
        }
        Entry& entry = mData[p.index];
        if (entry.generation != p.generation) {
            return nullptr;
        }
        return &entry.value;
    }

    const T* get(Pointer p) const {
        if (UTILS_UNLIKELY(p.index >= mData.size())) {
            return nullptr;
        }
        const Entry& entry = mData[p.index];
        if (entry.generation != p.generation) {
            return nullptr;
        }
        return &entry.value;
    }

private:
    static constexpr uint32_t FREE_FLAG = 0x80000000u;
    static constexpr uint32_t GENERATION_MASK = ~FREE_FLAG;
    static constexpr uint32_t kInvalidIndex = UINT32_MAX;

    struct Entry {
        uint32_t generation = FREE_FLAG;
        
        union {
            T value;
            uint32_t nextFree;
        };

        Entry() : nextFree(kInvalidIndex) {}
        // T is destroyed manually by Arena.
        ~Entry() {} 
    };

    FixedCapacityVector<Entry> mData;
    uint32_t mFreeHead = kInvalidIndex;
};

} // namespace utils

#endif // TNT_UTILS_GENERATIONAL_ARENA_H
