#pragma once

#include "noncopyable.h"
#include <semaphore.h>
#include <stdint.h>

class Semaphore : Noncopyable{
    public:
    Semaphore(uint32_t count = 0);

    ~Semaphore();

    void wait();

    void notify();

    private:
    sem_t m_semaphore;
};