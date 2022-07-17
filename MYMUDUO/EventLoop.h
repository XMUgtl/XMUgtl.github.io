#pragma once
#include<vector>
#include "noncopyable.h"
#include<atomic>
#include<functional>
#include"Timestamp.h"
#include <memory>
#include<mutex>
#include"CurrentThread.h"

class Channel;
class Poller;



class EventLoop
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

   
    void loop();
    
    void quit();

    Timestamp pollerReturnTime();

   
    void runInLoop(Functor cb);
    
    void queueInLoop(Functor cb);

    
    void wakeup();

    
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);
    
   
    bool isInLoopThread() const {return threadId_ == CurrentThread::tid();}

private:
    void handleRead();
    void doPendingFunctors();

    using ChannelList = std::vector<Channel*>;

    
    std::atomic_bool looping_;
    std::atomic_bool quit_;
    

    
    const pid_t threadId_;
    Timestamp pollReturnTime_;

    std::unique_ptr<Poller>poller_;


   
    int wakeupFd_;
    std::unique_ptr<Channel>wakeupChannel_;

    ChannelList activeChannels_;
    Channel *currentActiveChannel_;

    std::atomic_bool callingPendingFunctors_;
    std::vector<Functor>pendingFunctors_;
    std::mutex mutex_;

};

