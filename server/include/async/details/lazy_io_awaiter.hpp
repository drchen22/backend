#pragma once


class lazy_awaiter {
public:


    bool await_ready() {
        return false;
    }

protected:

};