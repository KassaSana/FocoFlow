/**
 * Neural Focus: Lock-Free Ring Buffer
 * 
 * This is the heart of our low-latency event pipeline. A ring buffer (circular queue)
 * that supports ONE producer thread and ONE consumer thread WITHOUT LOCKS.
 * 
 * DESIGN PATTERN: Single-Producer-Single-Consumer (SPSC) Lock-Free Queue
 * =======================================================================
 * 
 * Traditional Queue (with mutex):
 * ```
 * Producer Thread:          Consumer Thread:
 * lock(mutex);              lock(mutex);  ← BLOCKS waiting for producer!
 * enqueue(item);            dequeue(item);
 * unlock(mutex);            unlock(mutex);
 * ```
 * 
 * Problem: If producer holds lock, consumer waits (and vice versa).
 * Result: 10-50μs latency from mutex contention.
 * 
 * Lock-Free Queue (our approach):
 * ```
 * Producer Thread:          Consumer Thread:
 * head++;                   tail++;        ← No blocking!
 * buffer[head] = item;      item = buffer[tail];
 * ```
 * 
 * Benefit: 10-100ns latency (1000x faster).
 * Trade-off: More complex code, requires atomic operations.
 * 
 * WHY THIS WORKS:
 * ===============
 * Key insight: Producer only writes to head, Consumer only writes to tail.
 * No write conflicts = no need for locks!
 * 
 * Producer checks: "Is buffer full?" → Reads tail (safe, consumer owns it)
 * Consumer checks: "Is buffer empty?" → Reads head (safe, producer owns it)
 * 
 * EDUCATIONAL NOTE: The ABA Problem
 * ==================================
 * Classic lock-free bug:
 * 
 * Time  Producer           Consumer           head  tail
 * T0    Read tail=5        -                  10    5
 * T1    Compute head+1=11  -                  10    5
 * T2    -                  Read head=10       10    5
 * T3    -                  Advance tail=6     10    6
 * T4    Write head=11      -                  11    6   ← Overwrote un-consumed item!
 * 
 * Solution: Use 64-bit counters that never wrap (2^64 = 584 billion years).
 * Even at 1 billion ops/sec, takes 584 years to wrap.
 */

#ifndef NEUROFOCUS_RING_BUFFER_H
#define NEUROFOCUS_RING_BUFFER_H

#include <atomic>
#include <array>
#include <cstddef>
#include "event.h"

/**
 * LockFreeRingBuffer: High-performance circular queue
 * 
 * TEMPLATE PARAMETERS:
 * - T: Element type (Event in our case)
 * - Size: Capacity (MUST be power of 2 for fast modulo)
 * 
 * Why power of 2?
 * - Normal modulo: index % size → division (slow, ~20 cycles)
 * - Power-of-2 modulo: index & (size-1) → bitwise AND (fast, 1 cycle)
 * 
 * Example with Size=8 (binary 0b1000):
 * - size-1 = 7 (binary 0b0111) ← This is a mask!
 * - index=13 (0b1101) & 0b0111 = 0b0101 = 5
 * - Same result as 13 % 8 = 5, but 20x faster
 */
template<typename T, size_t Size>
class LockFreeRingBuffer {
    // Compile-time check: Size must be power of 2
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    static_assert(Size >= 2, "Size must be at least 2");
    
private:
    //==========================================================================
    // DATA MEMBERS
    //==========================================================================
    
    /**
     * head_: Producer writes here (incremented by producer thread)
     * 
     * EDUCATIONAL NOTE: Cache Line Alignment (alignas(64))
     * =====================================================
     * Problem: "False Sharing"
     * 
     * Without alignment:
     * [Cache Line at 0x1000]: [head_][tail_][other data...]
     * 
     * When producer writes head_, CPU invalidates entire cache line.
     * Consumer's cached copy of tail_ becomes stale → cache miss!
     * 
     * With alignment:
     * [Cache Line at 0x1000]: [head_][padding........................]
     * [Cache Line at 0x1040]: [tail_][padding........................]
     * 
     * Now head_ and tail_ are on DIFFERENT cache lines.
     * Producer writes head_ → only invalidates first cache line.
     * Consumer's tail_ cache is unaffected → no false sharing!
     * 
     * Performance impact: 2-10x faster under contention.
     */
    alignas(64) std::atomic<uint64_t> head_;
    
    /**
     * tail_: Consumer reads from here (incremented by consumer thread)
     * 
     * Separate cache line from head_ (see above explanation).
     */
    alignas(64) std::atomic<uint64_t> tail_;
    
    /**
     * buffer_: The actual storage (array of T)
     * 
     * Why std::array instead of raw array (T buffer_[Size])?
     * - std::array has bounds checking in debug mode (catches bugs)
     * - std::array works with standard algorithms (std::fill, etc.)
     * - Zero runtime overhead in release mode (same as raw array)
     */
    std::array<T, Size> buffer_;
    
public:
    //==========================================================================
    // CONSTRUCTOR
    //==========================================================================
    
    /**
     * Initialize head and tail to 0.
     * 
     * EDUCATIONAL NOTE: Memory Ordering (memory_order_relaxed)
     * =========================================================
     * std::atomic operations can have different "memory orderings":
     * 
     * 1. memory_order_relaxed: No synchronization
     *    - Fastest (no memory barriers)
     *    - Use when order doesn't matter (initialization)
     * 
     * 2. memory_order_acquire: Synchronize reads
     *    - Ensures all writes before this are visible
     *    - Use when consuming data produced by another thread
     * 
     * 3. memory_order_release: Synchronize writes
     *    - Ensures all writes are visible to other threads
     *    - Use when producing data for another thread
     * 
     * 4. memory_order_seq_cst: Full synchronization (default)
     *    - Slowest (full memory barriers)
     *    - Use when you need total ordering
     * 
     * For initialization, relaxed is fine (no other threads exist yet).
     */
    LockFreeRingBuffer() 
        : head_(0), tail_(0) 
    {
        // No need to initialize buffer_ (will be filled by producer)
    }
    
    //==========================================================================
    // PRODUCER API (called by event capture thread)
    //==========================================================================
    
    /**
     * try_push: Attempt to enqueue an item
     * 
     * Returns: true if successful, false if buffer full
     * 
     * ALGORITHM:
     * 1. Read current head (where we'll write)
     * 2. Compute next_head (head + 1)
     * 3. Read current tail (to check if full)
     * 4. If (next_head - tail) >= Size → Buffer full, return false
     * 5. Write item to buffer[head & (Size-1)]
     * 6. Update head to next_head
     * 7. Return true
     * 
     * MEMORY ORDERING:
     * - head.load(relaxed): We're the only writer, no sync needed
     * - tail.load(acquire): CRITICAL! Must see consumer's latest tail
     * - head.store(release): CRITICAL! Consumer must see our new head
     */
    bool try_push(const T& item) {
        // Step 1: Read current head (our write position)
        // relaxed: We're the only thread writing head_, so no sync needed
        const uint64_t current_head = head_.load(std::memory_order_relaxed);
        
        // Step 2: Compute next head position
        const uint64_t next_head = current_head + 1;
        
        // Step 3: Read current tail (consumer's read position)
        // acquire: MUST use acquire to see consumer's latest tail update
        // Without acquire, we might see stale tail → think buffer is full when it's not!
        const uint64_t current_tail = tail_.load(std::memory_order_acquire);
        
        // Step 4: Check if buffer is full
        // Full condition: (next_head - tail) >= Size
        // 
        // Why this formula?
        // Example: Size=8, head=10, tail=3
        // - next_head=11
        // - (11-3)=8 >= 8 → Full! (can't advance head without overwriting tail)
        // 
        // Example: Size=8, head=10, tail=4
        // - next_head=11
        // - (11-4)=7 < 8 → Not full, proceed
        if (next_head - current_tail >= Size) {
            return false;  // Buffer full, drop event
        }
        
        // Step 5: Write item to buffer
        // Use bitwise AND for fast modulo (head & (Size-1) == head % Size)
        const size_t index = current_head & (Size - 1);
        buffer_[index] = item;
        
        // Step 6: Publish new head
        // release: CRITICAL! Ensures buffer write is visible before head update
        // Without release, consumer might read new head but see old buffer data!
        // 
        // EDUCATIONAL NOTE: Reordering
        // ============================
        // CPU/compiler can reorder instructions for performance:
        // 
        // Without release:
        //   buffer_[index] = item;    ← Write A
        //   head_ = next_head;        ← Write B
        // 
        // CPU might execute: B, then A (reordered for efficiency)
        // Consumer sees new head, reads buffer BEFORE item is written → corrupt data!
        // 
        // With release:
        //   buffer_[index] = item;    ← Write A
        //   head_.store(next_head, memory_order_release);  ← Write B with barrier
        // 
        // CPU CANNOT reorder A after B (release acts as barrier)
        // Consumer is guaranteed to see item when it sees new head.
        head_.store(next_head, std::memory_order_release);
        
        return true;
    }
    
    /**
     * is_full: Check if buffer is full (without modifying state)
     * 
     * Used by monitoring code to track drop rate.
     */
    bool is_full() const {
        const uint64_t current_head = head_.load(std::memory_order_relaxed);
        const uint64_t current_tail = tail_.load(std::memory_order_acquire);
        return (current_head + 1 - current_tail) >= Size;
    }
    
    //==========================================================================
    // CONSUMER API (called by Python IPC thread)
    //==========================================================================
    
    /**
     * try_pop: Attempt to dequeue an item
     * 
     * Returns: true if successful, false if buffer empty
     * 
     * ALGORITHM:
     * 1. Read current tail (where we'll read)
     * 2. Read current head (to check if empty)
     * 3. If tail == head → Buffer empty, return false
     * 4. Read item from buffer[tail & (Size-1)]
     * 5. Update tail (tail + 1)
     * 6. Return true
     */
    bool try_pop(T& item) {
        // Step 1: Read current tail (our read position)
        const uint64_t current_tail = tail_.load(std::memory_order_relaxed);
        
        // Step 2: Read current head (producer's write position)
        // acquire: MUST see producer's latest head update
        const uint64_t current_head = head_.load(std::memory_order_acquire);
        
        // Step 3: Check if buffer is empty
        if (current_tail == current_head) {
            return false;  // Empty, nothing to read
        }
        
        // Step 4: Read item from buffer
        const size_t index = current_tail & (Size - 1);
        item = buffer_[index];
        
        // Step 5: Publish new tail
        // release: Ensures producer sees our tail update (for full check)
        tail_.store(current_tail + 1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * is_empty: Check if buffer is empty
     */
    bool is_empty() const {
        const uint64_t current_tail = tail_.load(std::memory_order_relaxed);
        const uint64_t current_head = head_.load(std::memory_order_acquire);
        return current_tail == current_head;
    }
    
    //==========================================================================
    // MONITORING API (called by stats thread)
    //==========================================================================
    
    /**
     * size: Current number of elements in buffer
     * 
     * WARNING: This is an approximation!
     * - head/tail are updated concurrently
     * - Returned value might be stale by the time you read it
     * - Use only for statistics, NOT for correctness
     */
    size_t size() const {
        const uint64_t current_head = head_.load(std::memory_order_acquire);
        const uint64_t current_tail = tail_.load(std::memory_order_acquire);
        return static_cast<size_t>(current_head - current_tail);
    }
    
    /**
     * capacity: Maximum number of elements
     */
    constexpr size_t capacity() const {
        return Size;
    }
    
    /**
     * utilization: Percentage full (0.0 to 1.0)
     * 
     * Used to monitor back-pressure:
     * - <50%: Healthy
     * - 50-80%: Consumer falling behind
     * - >80%: Approaching drops, investigate!
     */
    float utilization() const {
        return static_cast<float>(size()) / static_cast<float>(Size);
    }
};

/**
 * USAGE EXAMPLE:
 * ==============
 * 
 * // Create buffer for 65,536 events (2^16)
 * LockFreeRingBuffer<Event, 65536> event_buffer;
 * 
 * // Producer thread (C++ event capture)
 * void producer_thread() {
 *     while (true) {
 *         Event e = capture_event();  // Get event from Windows
 *         if (!event_buffer.try_push(e)) {
 *             // Buffer full! Log drop
 *             ++dropped_events;
 *         }
 *     }
 * }
 * 
 * // Consumer thread (Python IPC)
 * void consumer_thread() {
 *     while (true) {
 *         Event e;
 *         if (event_buffer.try_pop(e)) {
 *             send_to_python(e);  // Forward via ZeroMQ
 *         } else {
 *             // Buffer empty, wait a bit
 *             std::this_thread::sleep_for(std::chrono::microseconds(100));
 *         }
 *     }
 * }
 * 
 * PERFORMANCE CHARACTERISTICS:
 * ============================
 * - try_push: 10-50ns (no contention)
 * - try_pop: 10-50ns (no contention)
 * - Memory: Size * sizeof(T) (65536 * 64 bytes = 4MB)
 * - Throughput: ~50 million ops/sec on modern CPU
 * - Latency: <100ns p99 (vs. 10-50μs with mutex)
 * 
 * LIMITATIONS:
 * ============
 * 1. SPSC only: DOES NOT work with multiple producers or consumers
 *    - Multi-producer requires atomic CAS on head (slower)
 *    - Multi-consumer requires atomic CAS on tail (slower)
 * 
 * 2. Fixed size: Cannot grow dynamically
 *    - If Size too small → frequent drops
 *    - If Size too large → wastes memory
 *    - Solution: Tune based on load testing
 * 
 * 3. No blocking: try_push/try_pop return immediately
 *    - No "wait until space" or "wait until data"
 *    - Caller must handle failures (retry, drop, etc.)
 * 
 * WHY THIS IS SAFE:
 * =================
 * - Producer owns head_ write, Consumer owns tail_ write → No data races
 * - acquire/release memory ordering ensures visibility of buffer writes
 * - 64-bit counters never wrap in practice (584 years at 1 billion ops/sec)
 * - Power-of-2 size guarantees contiguous buffer (no index gaps)
 */

#endif // NEUROFOCUS_RING_BUFFER_H
