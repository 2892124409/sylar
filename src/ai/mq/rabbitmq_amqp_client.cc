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

struct RabbitMqAmqpClient::Session
{
    amqp_connection_state_t connection = nullptr;
    amqp_socket_t* socket = nullptr;
    uint16_t channel = 0;
};

RabbitMqAmqpClient::RabbitMqAmqpClient(const config::RabbitMqSettings& settings)
    : m_settings(settings)
{
}

bool RabbitMqAmqpClient::Publish(const std::string& payload, std::string& error) const
{
    Session session;
    if (!OpenSession(session, error))
    {
        return false;
    }

    if (!DeclareQueue(session, error))
    {
        CloseSession(session);
        return false;
    }

    amqp_basic_properties_t props;
    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.content_type = amqp_cstring_bytes("application/json");
    props.delivery_mode = 2;

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

    CloseSession(session);
    return true;
}

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

bool RabbitMqAmqpClient::Get(size_t count, std::vector<std::string>& payloads, std::string& error) const
{
    payloads.clear();

    Session session;
    if (!OpenSession(session, error))
    {
        return false;
    }

    if (!DeclareQueue(session, error))
    {
        CloseSession(session);
        return false;
    }

    for (size_t i = 0; i < count; ++i)
    {
        amqp_rpc_reply_t rpc = amqp_basic_get(session.connection,
                                              session.channel,
                                              amqp_cstring_bytes(m_settings.queue.c_str()),
                                              1);
        if (rpc.reply_type != AMQP_RESPONSE_NORMAL)
        {
            std::ostringstream oss;
            oss << "amqp_basic_get failed: " << amqp_error_string2(rpc.library_error);
            error = oss.str();
            CloseSession(session);
            return false;
        }

        if (rpc.reply.id == AMQP_BASIC_GET_EMPTY_METHOD)
        {
            break;
        }

        if (rpc.reply.id != AMQP_BASIC_GET_OK_METHOD)
        {
            error = "unexpected AMQP method in basic_get response";
            CloseSession(session);
            return false;
        }

        amqp_message_t message;
        amqp_rpc_reply_t msg_reply = amqp_read_message(session.connection, session.channel, &message, 0);
        if (msg_reply.reply_type != AMQP_RESPONSE_NORMAL)
        {
            error = "amqp_read_message failed: " + BuildServerError(session);
            CloseSession(session);
            return false;
        }

        payloads.push_back(BytesToString(message.body.bytes, message.body.len));
        amqp_destroy_message(&message);
    }

    CloseSession(session);
    return true;
}

bool RabbitMqAmqpClient::OpenSession(Session& session, std::string& error) const
{
    session.channel = m_settings.channel;
    session.connection = amqp_new_connection();
    if (!session.connection)
    {
        error = "amqp_new_connection failed";
        return false;
    }

    session.socket = amqp_tcp_socket_new(session.connection);
    if (!session.socket)
    {
        error = "amqp_tcp_socket_new failed";
        CloseSession(session);
        return false;
    }

    struct timeval timeout;
    timeout.tv_sec = static_cast<long>(m_settings.connect_timeout_ms / 1000);
    timeout.tv_usec = static_cast<long>((m_settings.connect_timeout_ms % 1000) * 1000);
    if (timeout.tv_sec == 0 && timeout.tv_usec == 0)
    {
        timeout.tv_usec = 1000;
    }

    int open_rt = amqp_socket_open_noblock(session.socket,
                                           m_settings.host.c_str(),
                                           static_cast<int>(m_settings.port),
                                           &timeout);
    if (open_rt != AMQP_STATUS_OK)
    {
        std::ostringstream oss;
        oss << "amqp_socket_open_noblock failed: " << amqp_error_string2(open_rt)
            << " host=" << m_settings.host << " port=" << m_settings.port;
        error = oss.str();
        CloseSession(session);
        return false;
    }

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

    amqp_channel_open(session.connection, session.channel);
    if (!CheckRpcReply("amqp_channel_open", session, error))
    {
        CloseSession(session);
        return false;
    }

    return true;
}

void RabbitMqAmqpClient::CloseSession(Session& session) const
{
    if (!session.connection)
    {
        return;
    }

    if (session.channel != 0)
    {
        amqp_channel_close(session.connection, session.channel, AMQP_REPLY_SUCCESS);
    }

    amqp_connection_close(session.connection, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(session.connection);

    session.connection = nullptr;
    session.socket = nullptr;
    session.channel = 0;
}

bool RabbitMqAmqpClient::DeclareQueue(Session& session, std::string& error) const
{
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

bool RabbitMqAmqpClient::CheckRpcReply(const char* action,
                                       const Session& session,
                                       std::string& error) const
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
