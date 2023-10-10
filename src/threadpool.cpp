#include "threadpool.h"

Thread::Thread(std::function<void()> func) 
    :m_func(func){

    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt != 0) {
        throw std::logic_error("pthread_create error");
    }
    m_semaphore.wait();
}

Thread::~Thread() {
    if(m_thread) {
        pthread_detach(m_thread);
    }
}

void Thread::join() {
    if(m_thread) {
        int rt = pthread_join(m_thread, nullptr);
        if(rt) {
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

void* Thread::run(void* arg) {
    Thread* thread = (Thread*)arg;


    std::function<void()> func;
    func.swap(thread->m_func);

    thread->m_semaphore.notify();

    func();
    return 0;
}
