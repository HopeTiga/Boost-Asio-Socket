#include <coroutine>
#include <iostream>
#include "LogicSystem.h"

class SystemCoroutine {
public:
    class promise_type {
    public:
        // ԭ��״̬��־
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

        // ״̬���ʽӿ�
        bool is_suspended() const noexcept {
            return suspended_.load(std::memory_order_acquire);
        }

        void storeIndex(int index) {
            coroIndex = index;
            readyQueue.push(index); // �����������
        }
    };

    // awaitable������
    class Awaitable {
    public:
        Awaitable() :suspended_(false) {};

        std::atomic<bool> suspended_;

        bool await_ready() {
            return false; // ���ǹ���
        };

        void await_suspend(std::coroutine_handle<promise_type> handle) {

            handle.promise().suspended_.store(false, std::memory_order_release);

			readyQueue.push(handle.promise().coroIndex); // �����������

            this->handle = handle;
        };

        void await_resume() {
            this->handle.promise().suspended_.store(true, std::memory_order_release);
        }

        std::coroutine_handle<promise_type> handle;
    };

    // �ⲿ״̬��ȡ�ӿ�
    bool is_suspended() const noexcept {
        return handle.promise().is_suspended();
    }

    SystemCoroutine() {};
    SystemCoroutine(std::coroutine_handle<promise_type> handle) :handle(handle) {};

    std::coroutine_handle<promise_type> handle;
};