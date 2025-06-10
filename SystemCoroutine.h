#include <coroutine>
#include <iostream>
#include "LogicSystem.h"

class SystemCoroutine {
public:
    class promise_type {
    public:
        // 原子状态标志
        std::atomic<bool> suspended_{ false };
        int coroIndex;
        auto initial_suspend() { return std::suspend_always{}; }
        auto final_suspend() noexcept { return std::suspend_always{}; }
        SystemCoroutine get_return_object() {
            std::coroutine_handle<promise_type> handle = std::coroutine_handle<promise_type>::from_promise(*this);
            return SystemCoroutine(handle);
        }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }

        // 状态访问接口
        bool is_suspended() const noexcept {
            return suspended_.load(std::memory_order_acquire);
        }

        void storeIndex(int index) {
            coroIndex = index;
            readyQueue.push(index); // 加入就绪队列
        }
    };

    // awaitable适配器
    class Awaitable {
    public:
        Awaitable() :suspended_(false) {};

        std::atomic<bool> suspended_;

        bool await_ready() {
            return false; // 总是挂起
        };

        void await_suspend(std::coroutine_handle<promise_type> handle) {

            handle.promise().suspended_.store(false, std::memory_order_release);

			readyQueue.push(handle.promise().coroIndex); // 加入就绪队列

            this->handle = handle;
        };

        void await_resume() {
            this->handle.promise().suspended_.store(true, std::memory_order_release);
        }

        std::coroutine_handle<promise_type> handle;
    };

    // 外部状态获取接口
    bool is_suspended() const noexcept {
        return handle.promise().is_suspended();
    }

    SystemCoroutine() {};
    SystemCoroutine(std::coroutine_handle<promise_type> handle) :handle(handle) {};

    std::coroutine_handle<promise_type> handle;
};