#ifndef __SYLAR_HTTP_SSE_H__
#define __SYLAR_HTTP_SSE_H__

#include "sylar/http/http_session.h"

#include <stdint.h>
#include <string>

namespace sylar
{
    namespace http
    {

        /**
         * @brief SSE 输出工具
         * @details
         * 这个类负责把普通字符串编码成 SSE 协议格式，并写到 HTTP 长连接上。
         *
         * 它本身不负责：
         * - 建立 HTTP 连接
         * - 管理客户端列表
         * - 广播消息
         *
         * 它只负责一件事：
         * 把“已经拿到的一条事件”按 SSE 格式写出去。
         */
        class SSEWriter
        {
        public:
            /// 绑定一个已经建立好的 HttpSession
            explicit SSEWriter(HttpSession::ptr session)
                : m_session(session)
            {
            }

            /**
             * @brief 发送一条标准 SSE 事件
             * @param data 事件数据体
             * @param event 事件名，可选
             * @param id 事件 ID，可选
             * @param retry 客户端建议重连时间，可选
             */
            int sendEvent(const std::string &data,
                          const std::string &event = "",
                          const std::string &id = "",
                          uint32_t retry = 0);

            /**
             * @brief 发送注释帧
             * @details
             * 常用于 SSE 心跳，例如 `: ping\n\n`
             */
            int sendComment(const std::string &comment);

        private:
            /// 绑定的 HTTP 会话连接，SSE 帧将通过该连接持续写回客户端
            HttpSession::ptr m_session;
        };

    } // namespace http
} // namespace sylar

#endif
