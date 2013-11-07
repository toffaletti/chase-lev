#include "optional.hpp"
#include "stdafx.h"

using namespace std;
using rl::var;
using experimental::optional;
using experimental::nullopt;
using experimental::in_place;
using experimental::in_place_t;
using experimental::is_not_optional;

template <typename T>
class ws_deque {
private:
    static size_t const     cacheline_size = 64;
    typedef char            cacheline_pad_t [cacheline_size];
    struct array;

    cacheline_pad_t pad0_;
    atomic<size_t> _top;
    atomic<size_t> _bottom;
    atomic<array *> _array;
    cacheline_pad_t pad1_;
public:
    ws_deque(size_t size = 2) : _top{0}, _bottom{0} {
        _array.store(new array{(size_t)log(size)}, memory_order_seq_cst);
    }

    ~ws_deque() {
        delete _array.load(memory_order_seq_cst);
    }

    // push_back
    void push(T x) {
        size_t b = _bottom.load(memory_order_seq_cst);
        size_t t = _top.load(memory_order_seq_cst);
        array *a = _array.load(memory_order_seq_cst);
        if (b - t > a->size() - 1) {
            a = a->grow(t, b).release();
            size_t ss = a->size();
            unique_ptr<array> old{_array.exchange(a, memory_order_seq_cst)};
            _bottom.store(b + ss, memory_order_seq_cst);
            t = _top.load(memory_order_seq_cst);
            if (!_top.compare_exchange_strong(t, t + ss,
                        memory_order_seq_cst, memory_order_seq_cst)) {
                _bottom.store(b, memory_order_seq_cst);
            }
            b = _bottom.load(memory_order_seq_cst);
        }
        a->put(b, x);
        _bottom.store(b + 1, memory_order_seq_cst);
    }

    // pop_back 
    optional<T> take() {
        size_t b = _bottom.load(memory_order_seq_cst) - 1;
        array *a = _array.load(memory_order_seq_cst);
        _bottom.store(b, memory_order_release);
        size_t t = _top.load(memory_order_seq_cst); 
        optional<T> x;
        if (t <= b) {
            // non-empty queue
            x = a->get(b);
            if (t == b) {
                // last element in queue
                if (!_top.compare_exchange_strong(t, t + 1,
                            memory_order_seq_cst, memory_order_seq_cst)) {
                    // failed race
                    x = nullopt;
                }
                _bottom.store(b + 1, memory_order_seq_cst);
            }
        } else {
            // empty queue
            //x = nullptr;
            _bottom.store(b + 1, memory_order_seq_cst);
        }
        return x;
    }

    // pop_front
    // pair of
    //  bool success? - false means try again
    //  optional<T> item
    pair<bool, optional<T> > steal() {
        optional<T> x;
        size_t t = _top.load(memory_order_seq_cst); 
        array *old_a = _array.load(memory_order_seq_cst);
        size_t b = _bottom.load(memory_order_seq_cst);
        array *a = _array.load(memory_order_seq_cst);
        ssize_t size = b - t;
        if (size <= 0) {
            // empty
            return make_pair(true, x);
        }
        if ((size % a->size()) == 0) {
            if (a == old_a && t == _top.load(memory_order_seq_cst)) {
                // empty
                return make_pair(true, x);
            } else {
                // abort, failed race
                return make_pair(false, nullopt);
            }

        }
        // non empty
        x = a->get(t);
        if (!_top.compare_exchange_strong(t, t + 1,
                    memory_order_seq_cst, memory_order_seq_cst)) {
            // failed race
            return make_pair(false, nullopt);
        }
        return make_pair(true, x);
    }

private:
    struct array {
        var<size_t> _size;
        var<atomic<T> *> _buffer;

        array(size_t size) {
            VAR(_size) = size;
            VAR(_buffer) = new atomic<T>[this->size()];
        }

        ~array() {
            delete[] VAR(_buffer);
        }

        size_t size() const {
            return 1<<VAR(_size);
        }

        void put(size_t i, T v) {
            VAR(_buffer)[i % size()].store(v, memory_order_seq_cst);
        }

        T get(size_t i) const {
            return VAR(_buffer)[i % size()].load(memory_order_seq_cst);
        }

        unique_ptr<array> grow(size_t top, size_t bottom) {
            unique_ptr<array> a{new array{VAR(_size)+1}};
            for (size_t i=top; i<bottom; ++i) {
                a->put(i, get(i));
            }
            return a;
        }
    };
};



struct ws_deque_test0 : rl::test_suite<ws_deque_test0, 4>
{
    ws_deque<int> q;

    void before()
    {
    }

    void after()
    {
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            for (size_t i = 0; i != 4; ++i)
            {
                q.push(10);
            }

            for (size_t i = 0; i != 5; ++i)
            {
                auto res = q.take();
                RL_ASSERT(10 == res.value_or(10));
            }

            for (size_t i = 0; i != 4; ++i)
            {
                q.push(10);
                auto res = q.take();
                RL_ASSERT(10 == res.value_or(10));
            }

            for (size_t i = 0; i != 4; ++i)
            {
                q.push(10);
                q.push(10);
                auto res = q.take();
                RL_ASSERT(10 == res.value_or(10));
                res = q.take();
                RL_ASSERT(10 == res.value_or(10));
            }

            for (size_t i = 0; i != 4; ++i)
            {
                q.push(10);
                q.push(10);
                q.push(10);
                auto res = q.take();
                RL_ASSERT(10 == res.value_or(10));
            }

            for (size_t i = 0; i != 14; ++i)
            {
                q.push(10);
                auto res = q.take();
                RL_ASSERT(10 == res.value_or(10));
            }
        }
        else
        {
            for (size_t i = 0; i != 4; ++i)
            {
                auto res = q.steal();
                RL_ASSERT(10 == res.second.value_or(10));
            }
        }
    }
};




struct ws_deque_test : rl::test_suite<ws_deque_test, 2>
{
    ws_deque<int> q;
    bool state [2];

    void before()
    {
        state[0] = true;
        state[1] = true;
    }

    void after()
    {
        RL_ASSERT(state[0] == false);
        RL_ASSERT(state[1] == false);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            q.push(1);
            q.push(2);

            auto res = q.take();
            RL_ASSERT(res && res.value_or(0) == 2);
            RL_ASSERT(state[1]);
            state[1] = false;

            res = q.take();
            if (res)
            {
                RL_ASSERT(state[0]);
                state[0] = false;
            }

            res = q.take();
            RL_ASSERT((bool)res == false);
        }
        else
        {
            auto res = q.steal();
            if (res.first && (bool)res.second)
            {
                RL_ASSERT(res.second.value_or(0) == 1);
                RL_ASSERT(state[0]);
                state[0] = false;
            }
        }
    }
};

int main()
{
    rl::simulate<ws_deque_test0>();
    rl::simulate<ws_deque_test>();
}
 
