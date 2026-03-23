#include "ai/mq/rabbitmq_amqp_client.h"

#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>

#include <algorithm>
#include <sstream>
#include <sys/time.h>

namespace ai
{
namespace mq
{

/**
 * @brief AMQP 会话运行时对象。
 * @details
 * 该结构只在一次操作生命周期内有效：
 * - Publish/Get/EnsureQueue 会创建 Session；
 * - 完成后统一 CloseSession 清理。
 */
struct RabbitMqAmqpClient::Session
{
    /** @brief AMQP 连接句柄。 */
    amqp_connection_state_t connection = nullptr;
    /** @brief AMQP TCP socket 句柄。 */
    amqp_socket_t* socket = nullptr;
    /** @brief 当前通道号。 */
    uint16_t channel = 0;
};

/**
 * @brief 构造并保存配置快照。
 */
RabbitMqAmqpClient::RabbitMqAmqpClient(const config::RabbitMqSettings& settings)
    : m_settings(settings)
{
}

/**
 * @brief 发布一条消息到 RabbitMQ。
 * @details
 * 执行顺序：
 * 1) 打开会话；
 * 2) 确保队列与绑定关系存在；
 * 3) 设置消息属性并发布；
 * 4) 关闭会话。
 */
bool RabbitMqAmqpClient::Publish(const std::string& payload, std::string& error) const
{
    // Step 1: 打开连接、登录、开通道。
    Session session;
    if (!OpenSession(session, error))
    {
        return false;
    }

    // Step 2: 幂等声明队列，避免首次发布时队列不存在。
    if (!DeclareQueue(session, error))
    {
        CloseSession(session);
        return false;
    }

    // Step 3: 准备消息属性。
    // - content_type=application/json: 便于消费者按 JSON 解析。
    // - delivery_mode=2: 持久化消息（broker 重启后可恢复，需队列/交换机配合 durable）。
    amqp_basic_properties_t props;
    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.content_type = amqp_cstring_bytes("application/json");
    props.delivery_mode = 2;

    // Step 4: 发布消息到 exchange + routing_key。
    // mandatory/immediate 均为 0，采用默认投递策略。
    const int rt = amqp_basic_publish(session.connection,
        session.channel,
        amqp_cstring_bytes(m_settings.exchange.c_str()),
        amqp_cstring_bytes(m_settings.routing_key.c_str()),
        0,
        0,
        &props,
        amqp_bytes_t{payload.size(), const_cast<char*>(payload.data())});
    if (rt != AMQP_STATUS_OK)
    {
        std::ostringstream oss;
        oss << "amqp_basic_publish failed: " << amqp_error_string2(rt);
        error = oss.str();
        CloseSession(session);
        return false;
    }

    // Step 5: 发布成功后关闭会话。
    CloseSession(session);
    return true;
}

/**
 * @brief 幂等声明队列。
 * @details
 * 该方法常用于启动阶段预热检查，也可在发布/消费前兜底调用。
 */
bool RabbitMqAmqpClient::EnsureQueue(std::string& error) const
{
    Session session;
    if (!OpenSession(session, error))
    {
        return false;
    }

    const bool ok = DeclareQueue(session, error);
    CloseSession(session);
    return ok;
}

/**
 * @brief 批量拉取消息（当前 no_ack）。
 * @details
 * 流程：
 * 1) 打开会话并声明队列；
 * 2) 循环 basic_get；
 * 3) 读取消息体并写入 payloads；
 * 4) 关闭会话。
 *
 * 注意：basic_get 的 no_ack=1，消息一旦被 broker 返回即视为已消费。
 */
bool RabbitMqAmqpClient::Get(size_t count, std::vector<std::string>& payloads, std::string& error) const
{
    // 调用方复用同一 vector 时，先清空历史内容。
    payloads.clear();

    // Step 1: 建立会话。
    Session session;
    if (!OpenSession(session, error))
    {
        return false;
    }

    // Step 2: 幂等声明队列。
    if (!DeclareQueue(session, error))
    {
        CloseSession(session);
        return false;
    }

    for (size_t i = 0; i < count; ++i)
    {
        // Step 3: 拉取单条消息。
        // no_ack=1: broker 无需等待 ack，会立即把消息从队列中移除。
        amqp_rpc_reply_t rpc =
            amqp_basic_get(session.connection, session.channel, amqp_cstring_bytes(m_settings.queue.c_str()), 1);
        if (rpc.reply_type != AMQP_RESPONSE_NORMAL)
        {
            std::ostringstream oss;
            oss << "amqp_basic_get failed: " << amqp_error_string2(rpc.library_error);
            error = oss.str();
            CloseSession(session);
            return false;
        }

        // 队列为空时返回 AMQP_BASIC_GET_EMPTY_METHOD，结束本次批量拉取。
        if (rpc.reply.id == AMQP_BASIC_GET_EMPTY_METHOD)
        {
            break;
        }

        // 期望方法应为 GET_OK，其他方法视作协议异常。
        if (rpc.reply.id != AMQP_BASIC_GET_OK_METHOD)
        {
            error = "unexpected AMQP method in basic_get response";
            CloseSession(session);
            return false;
        }

        // Step 4: 读取消息体数据。
        amqp_message_t message;
        amqp_rpc_reply_t msg_reply = amqp_read_message(session.connection, session.channel, &message, 0);
        if (msg_reply.reply_type != AMQP_RESPONSE_NORMAL)
        {
            error = "amqp_read_message failed: " + BuildServerError(session);
            CloseSession(session);
            return false;
        }

        // 将 AMQP bytes 转换为字符串 payload，供上层 JSON 解析。
        payloads.push_back(BytesToString(message.body.bytes, message.body.len));
        // 释放 librabbitmq 为 message 分配的内部资源。
        amqp_destroy_message(&message);
    }

    // Step 5: 本批次结束，关闭会话。
    CloseSession(session);
    return true;
}

/**
 * @brief 打开 AMQP 会话（连接 + 登录 + 开通道）。
 */
bool RabbitMqAmqpClient::OpenSession(Session& session, std::string& error) const
{
    // 使用配置中的固定 channel 编号。
    session.channel = m_settings.channel;

    // Step 1: 创建 connection 状态对象。
    session.connection = amqp_new_connection();
    if (!session.connection)
    {
        error = "amqp_new_connection failed";
        return false;
    }

    // Step 2: 在该 connection 上创建 TCP socket 对象。
    session.socket = amqp_tcp_socket_new(session.connection);
    if (!session.socket)
    {
        error = "amqp_tcp_socket_new failed";
        CloseSession(session);
        return false;
    }

    // Step 3: 构造连接超时（毫秒 -> timeval）。
    struct timeval timeout;
    timeout.tv_sec = static_cast<long>(m_settings.connect_timeout_ms / 1000);
    timeout.tv_usec = static_cast<long>((m_settings.connect_timeout_ms % 1000) * 1000);
    if (timeout.tv_sec == 0 && timeout.tv_usec == 0)
    {
        timeout.tv_usec = 1000;
    }

    // Step 4: 非阻塞方式建立 TCP 连接（带超时）。
    int open_rt =
        amqp_socket_open_noblock(session.socket, m_settings.host.c_str(), static_cast<int>(m_settings.port), &timeout);
    if (open_rt != AMQP_STATUS_OK)
    {
        std::ostringstream oss;
        oss << "amqp_socket_open_noblock failed: " << amqp_error_string2(open_rt) << " host=" << m_settings.host
            << " port=" << m_settings.port;
        error = oss.str();
        CloseSession(session);
        return false;
    }

    // Step 5: 登录 AMQP（PLAIN 认证）。
    // frame_max 固定为 131072；heartbeat 使用配置值。
    amqp_rpc_reply_t login_reply = amqp_login(session.connection,
        m_settings.vhost.c_str(),
        0,
        131072,
        m_settings.heartbeat_seconds,
        AMQP_SASL_METHOD_PLAIN,
        m_settings.username.c_str(),
        m_settings.password.c_str());
    if (login_reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        error = "amqp_login failed: " + BuildServerError(session);
        CloseSession(session);
        return false;
    }

    // Step 6: 打开逻辑通道，并校验 RPC 回复。
    amqp_channel_open(session.connection, session.channel);
    if (!CheckRpcReply("amqp_channel_open", session, error))
    {
        CloseSession(session);
        return false;
    }

    return true;
}

/**
 * @brief 关闭并销毁会话资源。
 * @details
 * 关闭失败不向上抛错（析构/收尾路径尽量 best-effort）。
 */
void RabbitMqAmqpClient::CloseSession(Session& session) const
{
    if (!session.connection)
    {
        return;
    }

    // 若通道有效，先关闭通道，再关闭连接。
    if (session.channel != 0)
    {
        amqp_channel_close(session.connection, session.channel, AMQP_REPLY_SUCCESS);
    }

    // 关闭连接并销毁 connection 状态对象。
    amqp_connection_close(session.connection, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(session.connection);

    // 清理本地句柄，避免悬空引用。
    session.connection = nullptr;
    session.socket = nullptr;
    session.channel = 0;
}

/**
 * @brief 声明队列并可选绑定交换机。
 * @details
 * 队列参数：
 * - passive=0: 若不存在则创建；
 * - durable=1: 持久化队列；
 * - exclusive=0: 非独占；
 * - auto_delete=0: 不自动删除。
 */
bool RabbitMqAmqpClient::DeclareQueue(Session& session, std::string& error) const
{
    // Step 1: 声明队列。
    amqp_queue_declare(session.connection,
        session.channel,
        amqp_cstring_bytes(m_settings.queue.c_str()),
        0,
        1,
        0,
        0,
        amqp_empty_table);

    if (!CheckRpcReply("amqp_queue_declare", session, error))
    {
        return false;
    }

    // Step 2: 若不是默认交换机，则显式执行 queue_bind。
    if (m_settings.exchange != "amq.default")
    {
        amqp_queue_bind(session.connection,
            session.channel,
            amqp_cstring_bytes(m_settings.queue.c_str()),
            amqp_cstring_bytes(m_settings.exchange.c_str()),
            amqp_cstring_bytes(m_settings.routing_key.c_str()),
            amqp_empty_table);
        if (!CheckRpcReply("amqp_queue_bind", session, error))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief 校验最近一次 RPC 调用是否成功。
 */
bool RabbitMqAmqpClient::CheckRpcReply(const char* action, const Session& session, std::string& error) const
{
    amqp_rpc_reply_t reply = amqp_get_rpc_reply(session.connection);
    if (reply.reply_type == AMQP_RESPONSE_NORMAL)
    {
        return true;
    }

    std::ostringstream oss;
    oss << action << " failed: " << BuildServerError(session);
    error = oss.str();
    return false;
}

/**
 * @brief 将 AMQP 错误结构转换为统一可读字符串。
 */
std::string RabbitMqAmqpClient::BuildServerError(const Session& session) const
{
    if (!session.connection)
    {
        return "connection is null";
    }

    const amqp_rpc_reply_t reply = amqp_get_rpc_reply(session.connection);
    std::ostringstream oss;

    if (reply.reply_type == AMQP_RESPONSE_NONE)
    {
        oss << "missing rpc reply type";
        return oss.str();
    }

    if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION)
    {
        oss << amqp_error_string2(reply.library_error);
        return oss.str();
    }

    if (!reply.reply.decoded)
    {
        oss << "server exception without detail";
        return oss.str();
    }

    if (reply.reply.id == AMQP_CONNECTION_CLOSE_METHOD)
    {
        amqp_connection_close_t* decoded = static_cast<amqp_connection_close_t*>(reply.reply.decoded);
        oss << "connection-close code=" << decoded->reply_code
            << " text=" << BytesToString(decoded->reply_text.bytes, decoded->reply_text.len);
        return oss.str();
    }

    if (reply.reply.id == AMQP_CHANNEL_CLOSE_METHOD)
    {
        amqp_channel_close_t* decoded = static_cast<amqp_channel_close_t*>(reply.reply.decoded);
        oss << "channel-close code=" << decoded->reply_code
            << " text=" << BytesToString(decoded->reply_text.bytes, decoded->reply_text.len);
        return oss.str();
    }

    oss << "server exception method_id=" << reply.reply.id;
    return oss.str();
}

/**
 * @brief AMQP 字节串转 std::string。
 */
std::string RabbitMqAmqpClient::BytesToString(const void* data, size_t len)
{
    if (!data || len == 0)
    {
        return std::string();
    }
    const char* c = static_cast<const char*>(data);
    return std::string(c, c + len);
}

} // namespace mq
} // namespace ai
