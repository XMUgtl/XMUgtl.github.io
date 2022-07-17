#include"TcpConnection.h"
#include"Logger.h"
#include"Socket.h"
#include"Channel.h"
#include"EventLoop.h"

#include<functional>
#include<errno.h>
#include<memory>
#include<sys/types.h>
#include<sys/socket.h>
#include<strings.h>
#include <netinet/tcp.h>
#include<string>

static EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if(loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d TcpConnection is null! \n",__FILE__,__FUNCTION__,__LINE__);
    }
    return loop;
}

 TcpConnection::TcpConnection(EventLoop *loop,const std::string &nameArg,int sockfd,const InetAddress& localAddr,const InetAddress& peerAddr)
    :loop_(CheckLoopNotNull(loop))
    ,name_(nameArg)
    ,state_(kConnecting)
    ,reading_(true)
    ,socket_(new Socket(sockfd))
    ,channel_(new Channel(loop,sockfd))
    ,localAddr_(localAddr)
    ,peerAddr_(peerAddr)
    ,highWaterMark_(64*1024*1024)
 {
     
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead,this,std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite,this));
    channel_->steCloseCallback(std::bind(&TcpConnection::handleClose,this));
    channel_->setErrorCallvack(std::bind(&TcpConnection::handleError,this));

    LOG_INFO("TcpConnection::ctor[%s] at fd = %d\n",name_.c_str(),sockfd);
    socket_->setKeepAlive(true);
 }

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd = %d state=%d \n",name_.c_str(),channel_->fd(),(int)state_);
}


void TcpConnection::send(const std::string &buf)
{
    if(state_ == kConnected)
    {
        
        if(loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(),buf.size());

        }
        else
        {
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop,this,buf.c_str(),buf.size()));
        }
    }
}



void TcpConnection::sendInLoop(const void*data,size_t len)
{
    ssize_t nwrote =0;
    size_t remaining =len;
    bool faultError = false;

    
    if(state_ == kDisconnected)
    {
        LOG_ERR("disconnected,give up writing");
        return;
    }
    
    
    if(!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(),data,len);
        if(nwrote >= 0)
        {
            remaining = len - nwrote;
            if(remaining == 0 && writeCompleteCallback_)
            {
                
                loop_->queueInLoop(std::bind(writeCompleteCallback_,shared_from_this()));
            }
        }
        else
        {
            nwrote = 0;
            if(errno != EWOULDBLOCK)
            {
                LOG_ERR("TcpConnection::sendInLoop");
                if(errno == EPIPE || errno == ECONNRESET)
                {
                    faultError = true;
                }
            }
        }
    }

    //说明当前这一次write，并没有把数据全部发送出去，剩余的数据需要保存到缓冲区中，然后给channel注册epollout事件（writing事件），poller发现tcp的发送缓冲区有空间，
    //会通知相应的sock(也就是有相应sock的channel)，调用handlewWrite回调方法
    //也就是调用TcpConnection::handleWrite方法，把发送缓冲区中的数据全部发送完成；如果后面还是有剩余数据没发送完就继续走这个步骤
    if(!faultError && remaining > 0)
    {
        //目前发送缓冲区剩余的待发送数据的长度
        size_t oldLen = outputBuffer_.readableBytes();
        if(oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkCallback_)//如果oldLen > highWaterMark_说明上一次就调用过高水位回调了；highWaterMarkCallback_表示这个回调已经有了
        {
            loop_->queueInLoop(std::bind(highWaterMarkCallback_,shared_from_this(),oldLen+remaining));//调用水位线回调
        }
        outputBuffer_.append((char*)data + nwrote , remaining);//把待发送数据用append()继续添加到缓冲区中
        if(!channel_->isWriting())//刚开始还没有对写事件感兴趣
        {
            channel_->enableWriting();//这里一定要注册channel的写事件，否则poller不会给channel通知epollout，就无法去驱动channel调用他的writeCallback,就无法最终去调用TcpConnection::handleWrite发送缓冲区的数据全部发送完成
        }
    }
}

//这里有一种特殊情况，就是待发送缓冲区有数据没有发送完成的话用户就调用了shutdown()，之后setState(kDisconnecting)这个会执行，而loop_->runInLoop(）也就是shutdownInLoop()
//不会被执行，因为里面的if语句（!channel_->isWriting()）不成立判断语句里面还是对写事件感兴趣的。此时底下handleWrite()正在执行，等数据发送完以后会执行if(state_ == kDisconnecting)
//这一句 能执行这一句就说明出现这种情况，即发送过程中执行shutdown()了，然后执行if语句里面的 即调用shutdownInLoop()，这样就把shutdown()过程全部走完。
/*
shutdown()的全过程：首先，设置服务器连接状态；然后执行shutdownInLoop()；因为shutdownInLoop()关闭了socket的write端Poller就给Channel通知了关闭事件，那么就会回调TcpConnection
的handleClose()方法，handleClose()方法相当于把channel的所有事件都去掉了
*/
//关闭连接
void TcpConnection::shutdown()//muduo库都是通过Loop来操作的，都写在反应堆里面的，EventLoop就是一个反应堆，他总是考虑当前loop有没有在他执行的线程里面，每一个loop所执行的方法都要在loop所执行的线程里去执行
{
    if(state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop,this));
    }
}

void TcpConnection::shutdownInLoop()
{
    if(!channel_->isWriting())//说明outputBuffer中的数据已经全部发送完成
    {
        //关闭写端会触发socket对应的channel的EPOLLHUP，EPOLLHUP事件是不用专门向epoll注册的，本身epoll就是向所有sockfd注册过这个事件了，紧接着对Channel::handleEventWithGuard()
        //里面进行分析，这里的笔记要转过去看
        socket_->shutdownWrite();//关闭写端
    }
}

/*
connectEstablished()做的事情就是setState更改状态为kConnected连接建立，然后把当前的跟客户端通信的channel直接enableReading相当于向poller注册channel的epollin事件(可读事件)，poller就开始监听
channel上的事件了，监听事件以后connectionCallback_(shared_from_this())这里就执行回调了；用户预制的onconnection方法就会调用到了，然后在那里面比如调用connected方法就会
看到连接已经建立成功；*/
//TcpConnection是对外提供的，用户可能对connection做任何的操作，我们底层的channel既然依赖于一个TcpConnection,
//所以就要防止用户的一些非法操作把（channel正在Poller中进行）把channl对应的上层TcpConnection对象给remove掉,就出问题了，所以这里就有channel_->tie(shared_from_this());这一句
//建立连接；创建连接的时候调用
void TcpConnection::connectEstablished()
{
    //首先设置连接状态；然后连接底层绑定管理的一个channel,让channel跟Tcp记录了一下connection的对象的一个存活的状态，毕竟TcpConnection是要给到用户手里的，我们的Chaneel和底层的Socket和Accept
    //都是不会给到用户的，所以这些东西的对象的生存周期是可控的；TcpConnection是给到用户手里，这涉及到底层channel的回调，调用的是TcpConnection的成员方法这里边就危险了，强弱智能指针
    //搭配既可以监控对象的存活状态而且可以解决多线程访问共享对象的一个现状安全问题
    setState(kConnected);//设置状态
    channel_->tie(shared_from_this());
    channel_->enableReading();//向poller注册channel的epollin事件（读事件），poller就开始监听channel上的事件

    //新连接建立，执行回调
    connectionCallback_(shared_from_this());
}


//连接销毁；连接关闭的时候调用
void TcpConnection::connectDestroyed()
{
    if(state_ == kConnected)//状态是已建立连接才能销毁
    {
        setState(kDisconnected);
        channel_->disableAll();//把channel的所有感兴趣的事件，从poller中del掉
        connectionCallback_(shared_from_this());
    }
    channel_->remove();//把channel从poller中删除掉
}

//channel上fd有数据可读了
void TcpConnection::handleRead(Timestamp receieveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(),&savedErrno);
    if(n>0)//有数据了
    {
        //已建立连接的用户，有可读的事件发生了，调用用户传入的回调操作onMessage
        messageCallback_(shared_from_this(),&inputBuffer_,receieveTime);//shared_from_this()就是获取当前TcpConnection对象的智能指针
    }
    else if (n == 0)//相当于客户端断开了
    {
        handleClose();
    }
    else
    {
        errno = savedErrno;
        LOG_ERR("TcpConnection::handleRead");
        handleError();
    }

}
void TcpConnection::handleWrite()
{
    if(channel_->isWriting())//判断一下是否可写
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(),&savedErrno);
        if(n>0)
        {
            outputBuffer_.retrieve(n);
            if(outputBuffer_.readableBytes()==0)//表示发送完成
            {
                channel_->disableWriting();//不可写
                if(writeCompleteCallback_)
                {
                    //唤醒loop_对应的thread线程，执行回调
                    loop_->queueInLoop(std::bind(writeCompleteCallback_,shared_from_this()));
                }
                if(state_ == kDisconnecting)//正在关闭，可能客户端断开了或者服务器调用shutdown
                {
                    shutdownInLoop();//在当前所属的loop里面把TcpConnection给删除掉
                }
            }
        }
        else
        {
            LOG_ERR("TcpConnection::handleWrite");
        }
    }
    else//此时不可写
    {
        LOG_ERR("TcpConnection fd = %d is down,no more writing \n",channel_->fd());
    }
}

//底层的Poller通知Channel调用他的closeCallback方法，在这里最终就回调到TcpConnection::handleClose()方法
void TcpConnection::handleClose()
{
    LOG_INFO("fd = %d state = %d \n",channel_->fd(),(int)state_);
    setState(kDisconnected);//设置状态为断开连接
    channel_->disableAll();//对所有事情都不感兴趣，在Poller上所有事情通过epoll删除掉

    TcpConnectionPtr connPtr(shared_from_this());//获取当前TcpConnection对象

    connectionCallback_(connPtr);//执行连接关闭的回调
    closeCallback_(connPtr);//关闭连接的回调 ；；执行的是TcpServer::removeConnection回调方法
}
void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if(::getsockopt(channel_->fd(),SOL_SOCKET,SO_ERROR,&optval,&optlen)<0)//getsockopt获取一个套接字的选项，就是获取channel_->fd()的状态 此时为获取其具体的错误状态
    {
        err = errno;
    }
    else
    { 
        err = optval;
    }
    LOG_ERR("TcpConnection::handleError name:%s - SO_ERROR:%d \n",name_.c_str(),err);
}
