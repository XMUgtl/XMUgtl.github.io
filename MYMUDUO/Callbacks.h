#pragma once

#include<memory>
#include<functional>

class Buffer;
class TcpConnection;
class Timestamp;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;//指向TcpConnection对象的智能指针
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;
using WriteCompleteCallback = std::function<void (const TcpConnectionPtr&)>;
using MessageCallback = std::function<void (const TcpConnectionPtr&,Buffer*,Timestamp)>;

using HighWaterMarkCallback = std::function<void(const TcpConnectionPtr&,size_t)>;