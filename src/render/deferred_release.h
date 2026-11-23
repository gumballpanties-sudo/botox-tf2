#pragma once
// Graveyard for textures swapped out between the frame build and Present (render.cpp).
// Present draws lists built earlier this frame, and re-draws them as-is on a frame that builds
// nothing (loading). So a swapped-out texture dies only after a LATER built frame replaced the
// lists: left = 2 built frames (the one it died in may still bind it, the next one does not).
// Tested by tests/deferred_release_test.cpp. T = anything with Release() (IUnknown).
#include <algorithm>
#include <atomic>
#include <vector>

template <class T>
struct DeferredReleaseQ
{
    struct Entry { T* p; int left; };
    std::vector<Entry> q;

    void Push(T* p) { if (p) q.push_back({ p, 2 }); }

    // end of Present; built = BuildImGuiFrame ran this frame
    void Age(bool built)
    {
        if (!built || q.empty()) return;
        std::erase_if(q, [](Entry& e) {
            if (--e.left > 0) return false;
            e.p->Release();
            return true;
        });
    }

    // Reset / device change / eject: lists are dropped, nothing can bind these any more
    void Flush()
    {
        for (Entry& e : q) e.p->Release();
        q.clear();
    }
};

// A slot shown by ImGui that owns ONE ref on what it holds. tex comes with a ref (GetContainer's):
// it moves into the slot, the one held before goes to q. Same tex again = drop the extra ref.
// Why owned: the engine frees + remakes every RT on a release/reacquire (map load RT alloc, a new
// RT mid match) with no device Reset, and a raw pointer then dangles under ImGui (esp preview crash).
template <class T, class Q>
void SwapOwned(std::atomic<T*>& slot, T* tex, Q& q)
{
    T* const old = slot.exchange(tex);
    if (old != tex) q.Push(old);
    else if (tex) tex->Release();
}
