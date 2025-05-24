#include <coroutine>
#include <iostream>

class SystemCoroutline {
public:
    class promise_type {
    public:
        // ԭ��״̬��־
        std::atomic<bool> suspended_{ false };
        auto initial_suspend() { return std::suspend_always{}; }
        auto final_suspend() noexcept { return std::suspend_always{}; }
        SystemCoroutline get_return_object() {
            std::coroutine_handle<promise_type> handle = std::coroutine_handle<promise_type>::from_promise(*this);
            return SystemCoroutline(handle);
        }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }

        // ״̬���ʽӿ�
        bool is_suspended() const noexcept {
            return suspended_.load(std::memory_order_acquire);
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

    SystemCoroutline() {};
    SystemCoroutline(std::coroutine_handle<promise_type> handle) :handle(handle) {};

    std::coroutine_handle<promise_type> handle;
};