#pragma once 



class noncopyable
{
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator = (const noncopyable&) = delete;

protected://派生类可以访问，外部无法访问
    noncopyable() = default;
    ~noncopyable() = default;
};