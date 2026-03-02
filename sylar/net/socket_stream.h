/**
 * @file socket_stream.h
 * @brief Socket 流式接口封装
 * @author sylar.yin
 * @date 2019-06-06
 */

#ifndef __SYLAR_SOCKET_STREAM_H__
#define __SYLAR_SOCKET_STREAM_H__

#include "stream.h"
#include "socket.h"

namespace sylar
{

    /**
     * @brief Socket 流
     * @details 将 Socket 包装成 Stream 接口，继承 readFixSize/writeFixSize
     */
    class SocketStream : public Stream
    {
    public:
        typedef std::shared_ptr<SocketStream> ptr;

        /**
         * @brief 构造函数
         * @param[in] sock Socket 类
         * @param[in] owner 是否完全控制（析构时是否关闭 Socket）
         */
        SocketStream(Socket::ptr sock, bool owner = true);

        /**
         * @brief 析构函数
         * @details 如果 m_owner=true，则 close
         */
        ~SocketStream();

        /**
         * @brief 读取数据
         * @param[out] buffer 待接收数据的内存
         * @param[in] length 待接收数据的内存长度
         * @return
         *      @retval >0 返回实际接收到的数据长度
         *      @retval =0 socket 被远端关闭
         *      @retval <0 socket 错误
         */
        virtual int read(void *buffer, size_t length) override;

        /**
         * @brief 读取数据
         * @param[out] ba 接收数据的 ByteArray
         * @param[in] length 待接收数据的内存长度
         * @return
         *      @retval >0 返回实际接收到的数据长度
         *      @retval =0 socket 被远端关闭
         *      @retval <0 socket 错误
         */
        virtual int read(ByteArray::ptr ba, size_t length) override;

        /**
         * @brief 写入数据
         * @param[in] buffer 待发送数据的内存
         * @param[in] length 待发送数据的内存长度
         * @return
         *      @retval >0 返回实际发送的数据长度
         *      @retval =0 socket 被远端关闭
         *      @retval <0 socket 错误
         */
        virtual int write(const void *buffer, size_t length) override;

        /**
         * @brief 写入数据
         * @param[in] ba 待发送数据的 ByteArray
         * @param[in] length 待发送数据的内存长度
         * @return
         *      @retval >0 返回实际发送的数据长度
         *      @retval =0 socket 被远端关闭
         *      @retval <0 socket 错误
         */
        virtual int write(ByteArray::ptr ba, size_t length) override;

        /**
         * @brief 关闭 socket
         */
        virtual void close() override;

        /**
         * @brief 返回 Socket 类
         */
        Socket::ptr getSocket() const { return m_socket; }

        /**
         * @brief 返回是否连接
         */
        bool isConnected() const;

        /**
         * @brief 返回远端地址
         */
        Address::ptr getRemoteAddress();

        /**
         * @brief 返回本地地址
         */
        Address::ptr getLocalAddress();

        /**
         * @brief 返回远端地址字符串
         */
        std::string getRemoteAddressString();

        /**
         * @brief 返回本地地址字符串
         */
        std::string getLocalAddressString();

    protected:
        /// Socket 类
        Socket::ptr m_socket;
        /// 是否主控（是否拥有 Socket 所有权）
        bool m_owner;
    };

}

#endif
