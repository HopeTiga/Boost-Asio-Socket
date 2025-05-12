#pragma once
#include <memory>
#include <iostream>
#include <mutex>

template<class T>
class Singleton {

protected:
    Singleton(const Singleton&) = delete;

    void operator=(const Singleton& single) = delete;

    Singleton() = default;

    virtual ~Singleton();

    static std::shared_ptr<T> instance;
public:

    static std::shared_ptr<T> getInstance();

    void printAddress();

};

template<class T>
std::shared_ptr<T> Singleton<T>::getInstance() {

    static std::once_flag flag;
    //call_once多线程中只会调用一次;
    std::call_once(flag, [=]() {

        instance = std::shared_ptr<T>(new T);

        });

    return instance;

}

template<class T>
Singleton<T>::~Singleton() {

    std::cout << "This Signleton was destructed ! " << std::endl;

}
template<class T>
void Singleton<T>::printAddress() {

    std::cout << "Singleton ptr : " << instance.get() << std::endl;

}

template<class T>
std::shared_ptr<T> Singleton<T>::instance = nullptr;