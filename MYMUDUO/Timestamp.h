#pragma once

#include <iostream>

class Timestamp
{
public:
    Timestamp();
    explicit Timestamp(int64_t microSecondsSinceEpoch);
    static Timestamp now();
    std::string tostring() const;
private:
    int64_t microSecondsSinceEpoch_;
};