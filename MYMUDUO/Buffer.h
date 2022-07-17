#pragma once

#include<vector>
#include<string>


class Buffer
{
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        :buffer_(kCheapPrepend + initialSize)
        ,readerIndex_(kCheapPrepend)
        ,writerIndex_(kCheapPrepend)
        {}
    
    size_t readableBytes()const 
    {
        return writerIndex_ - readerIndex_;
    }

    size_t writableBytes()const 
    {
        return buffer_.size() - writerIndex_;
    }

    size_t prependableBytes()const
    {
        return readerIndex_;
    }

   
    const char* peek() const
    {
        return begin() + readerIndex_;
    }

    
    void retrieve(size_t len)
    {
        if(len < readableBytes())
        {
            readerIndex_ += len;
        }
        else
        {
            retrieveAll();
        }
    }

    
    void retrieveAll()
    {
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }

    //把onMessage函数上报的Buffer数据，转成string类型的数据返回，给我们应用所用
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes());//应用可读取数据的长度
    }

    std::string retrieveAsString(size_t len)
    {
        std::string result(peek(),len);
        retrieve(len);
        return result;
    }

    
    void ensureWriteableBytes(size_t len)
    {
        if(writableBytes()<len)//这种情况就要扩容
        {
            makeSpace(len);//扩容函数
        }
    }

    
    
    void append(const char *data,size_t len)
    {
        ensureWriteableBytes(len);
        std::copy(data,data+len,beginWrite());
        writerIndex_ += len;
    }

    char* beginWrite()
    {
        return begin() + writerIndex_;
    }

    const char* beginWrite()const
    {
        return begin() + writerIndex_;
    }

    ssize_t readFd(int fd,int* saveErrno);
    
    ssize_t writeFd(int fd,int* saveErrno);
private:

    char* begin()
    {
       
        return &*buffer_.begin();
    }
    const char* begin()const
    {
        return &*buffer_.begin();
    }

    void makeSpace(size_t len)//扩容函数
    {
      
        if(writableBytes() + prependableBytes() < len + kCheapPrepend)
        {
            buffer_.resize(writerIndex_ + len);
        }
        else//当被读取部分空闲出来以后加上可写部分大于len，那么就将可读部分数据往前挪，然后就能把空出的部分补给writer,这样就够用了
        {
            size_t readable = readableBytes();//未读数据大小
            std::copy(begin() + readerIndex_,begin() + writerIndex_,begin() + kCheapPrepend);//（要复制数据的首地址，要复制数据的尾地址，要放置数据处的首地址）
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
        }
    }


    
    std::vector<char>buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};