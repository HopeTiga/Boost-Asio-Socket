#pragma once
#include <coroutine>
#include <iostream>
#include <boost/lockfree/queue.hpp>

class SystemCoroutine {
public:
    class promise_type {
    public:
        // 原子状态标志
        std::atomic<bool> suspended_{ false };
        int coroIndex;

        // 添加队列指针成员
        boost::lockfree::queue<int>* targetQueue = nullptr;

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
            // 使用指定的队列，如果没有指定则使用默认队列
            if (targetQueue != nullptr) {
                targetQueue->push(index);
            }
        }

        // 设置目标队列
        void setTargetQueue(boost::lockfree::queue<int>* queue) {
            targetQueue = queue;
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

            // 使用指定的队列，如果没有指定则使用默认队列
            if (handle.promise().targetQueue != nullptr) {
                handle.promise().targetQueue->push(handle.promise().coroIndex);
            }

            this->handle = handle;
        };

        void await_resume() {
            // 添加安全检查
            if (!handle || !handle.address()) {
                return; // handle无效，直接返回
            }

            try {
                this->handle.promise().suspended_.store(true, std::memory_order_release);
            }
            catch (std::exception& e) {
                std::cout << "SystemCoroutine::Awaitabl::await_resume Error : " << e.what() << "\r\n";
            }
        }

        std::coroutine_handle<promise_type> handle;
    };

    // 外部状态获取接口
    bool is_suspended() const noexcept {
        return handle.promise().is_suspended();
    }

    SystemCoroutine() {};
    SystemCoroutine(std::coroutine_handle<promise_type> handle) :handle(handle) {};

    // 设置协程使用的队列
    void setQueue(boost::lockfree::queue<int>* queue) {
        if (handle) {
            handle.promise().setTargetQueue(queue);
        }
    }

    std::coroutine_handle<promise_type> handle;
};