#include "teckos/client.h"
#include "teckos/global.h"   // for global
#include <exception>         // for exception
#include <mutex>             // for lock_guard
#include <nlohmann/json.hpp> // for json_ref
#include <utility>           // for move

#include "spdlog/spdlog.h" // Better logging

class ConnectionException : public std::exception {
  public:
    ConnectionException(int code, std::string const& reason) : code_(code), reason_(reason) {}

    int code() const { return code_; }
    std::string reason() const { return reason_; }

  private:
    int code_;
    std::string reason_;
};

teckos::client::client()
    : fn_id_(0), was_connected_before_(false), reconnecting_(false), connected_(false), authenticated_(false),
      timeout_(std::chrono::milliseconds(500))
{
    teckos::global::init();
#ifdef USE_IX_WEBSOCKET
    ix::initNetSystem();
    ws_ = std::make_unique<WebSocketClient>();
    ws_->enablePong();
    ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        switch(msg->type) {
        case ix::WebSocketMessageType::Message: {
            if(connected_) {
                handleMessage(msg->str);
            }
            break;
        }
        case ix::WebSocketMessageType::Close: {
            connected_ = false;
            authenticated_ = false;
            auto abnormal_exit = msg->closeInfo.code != 1000;
            auto disconnected_handler = lockedMemberCopy(disconnected_handler_);
            if(disconnected_handler) {
                disconnected_handler(abnormal_exit);
            }
            break;
        }
        case ix::WebSocketMessageType::Open: {
            connected_ = true;
            if(info_.hasJwt) {
                // Not connected without the jwt being validated
                nlohmann::json p = info_.payload;
                p["token"] = info_.jwt;
                return send("token", p);
            } else {
                authenticated_ = true;
                // Without jwt we are connected now
                if(reconnecting_) {
                    auto reconnected_handler = lockedMemberCopy(reconnected_handler_);
                    if(reconnected_handler) {
                        reconnected_handler();
                    }
                } else {
                    auto connected_handler = lockedMemberCopy(connected_handler_);
                    if(connected_handler) {
                        connected_handler();
                    }
                }
            }
            break;
        }
        case ix::WebSocketMessageType::Error: {
            std::cerr << msg->errorInfo.reason << std::endl;
            break;
        }
        case ix::WebSocketMessageType::Ping:
            break;
        case ix::WebSocketMessageType::Pong:
            break;
        case ix::WebSocketMessageType::Fragment:
            break;
        }
    });
#endif
}

teckos::client::~client()
{
    disconnect();
}

void teckos::client::on(const std::string& event, const std::function<void(const nlohmann::json&)>& handler)
{
    std::lock_guard<std::mutex> lock(event_handler_mutex_);
    // TODO: Support multiple handlers for a single event
    assert(event_handlers_.find(event) == event_handlers_.end());
    event_handlers_[event] = handler;
}

/*void teckos::client::off(const std::string& event)
{
    std::lock_guard<std::mutex> lock(event_handler_mutex_);
    event_handlers_.erase(event);
}*/

void teckos::client::connect(const std::string& url) noexcept(false)
{
    info_ = {url, false, "", ""};
    if(connected_) {
        disconnect();
    }
    was_connected_before_ = false;
    connect();
}

void teckos::client::connect(const std::string& url, const std::string& jwt,
                             const nlohmann::json& initialPayload) noexcept(false)
{
    info_ = {url, true, jwt, initialPayload};
    if(connected_) {
        disconnect();
    }
    was_connected_before_ = false;
    connect();
}

#ifdef USE_IX_WEBSOCKET
void teckos::client::connect()
{
    // Create new websocket client and attach handler
    ws_->setUrl(info_.url);
    // Connect
    if(settings_.reconnect) {
        ws_->enableAutomaticReconnection();
    } else {
        ws_->disableAutomaticReconnection();
    }
    connected_ = false;
    authenticated_ = false;
    ws_->start();
}
#else
void teckos::client::connect()
{
    // Connect
    connected_ = false;
    authenticated_ = false;
    std::shared_ptr<WebSocketClient> websocket;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        websocket_ = std::make_shared<WebSocketClient>();
        websocket = websocket_;
    }
    websocket->set_message_handler([this](const web::websockets::client::websocket_incoming_message& ret_msg) {
        try {
            auto msg = ret_msg.extract_string().get();
            handleMessage(msg);
        }
        catch(...) {
            spdlog::error("Unhandled exception occurred when parsing incoming "
                "message or calling handleMessage:");
        }
    });
    websocket->set_close_handler([this](web::websockets::client::websocket_close_status close_status,
                                        const utility::string_t& /* reason */, const std::error_code& /* code */) {
        connected_ = false;
        if(close_status == web::websockets::client::websocket_close_status::abnormal_close) {
            if(!reconnecting_ && was_connected_before_) {
                // Start reconnecting
                startReconnecting();
            }
        } else {
            // Graceful disconnect, inform handler (if any)
            auto disconnected_handler = lockedMemberCopy(disconnected_handler_);
            if(disconnected_handler) {
                disconnected_handler(true);
            }
        }
    });
    try {
        websocket_->connect(utility::conversions::to_string_t(info_.url)).wait();
        connected_ = true;
        was_connected_before_ = true;
        auto connected_handler = lockedMemberCopy(connected_handler_);
        if(connected_handler) {
            connected_handler();
        }
        if(info_.hasJwt) {
            nlohmann::json payload = info_.payload;
            payload["token"] = info_.jwt;
            send("token", payload);
        }
    }
    catch(web::websockets::client::websocket_exception& e) {
        connected_ = false;
        throw ConnectionException(e.error_code().value(), e.what());
    }
}
#endif

void teckos::client::disconnect()
{
#ifndef USE_IX_WEBSOCKET
    stopReconnecting();
#endif
    if(connected_) {
#ifdef USE_IX_WEBSOCKET
        ws_->stop(1000, "Normal Closure");
#else
        // Now close connecting, if it is still there
        {
            std::lock_guard<std::mutex> lock(mutex_);
        }
        assert(websocket_); // If this asserts you are doing disconnect too late
        if(websocket_) {
            websocket_->close(web::websockets::client::websocket_close_status::normal).wait();
        }
#endif
    }
    connected_ = false;
}

template <typename T>
T get_json(nlohmann::json const& json, const nlohmann::json::object_t::key_type& key) {
    return json[key];
}

template <> int get_json(nlohmann::json const& json, const nlohmann::json::object_t::key_type& key)
{
    if (!json[key].is_number_integer()) {
        throw teckos::json_message_error("expected an integer");
    }
    return json[key];
}

void teckos::client::handleMessage(const std::string& msg) noexcept
{
    if(msg == "hey") {
        return;
    }
#ifdef DEBUG_TECKOS_RECV
    spdlog::debug("teckos:receive << {}", msg);
#endif
    try {
        auto j = nlohmann::json::parse(msg);

        const PacketType type = static_cast<PacketType>(get_json<int>(j, "type")); // This will throw a json::exception in case the key is not valid
        switch(type) {
        case PacketType::EVENT: {
            const nlohmann::json jsonData = j["data"];
            if(jsonData.is_array() && !jsonData.empty()) {
                std::vector<nlohmann::json> data = j["data"];
                const std::string event = data[0];
                if(event == "ready") {
                    // ready is sent by server to inform that client is connected and the
                    // token valid
                    authenticated_ = true;
                    if(reconnecting_) {
                        auto reconnected_handler = lockedMemberCopy(reconnected_handler_);
                        if(reconnected_handler) {
                            reconnected_handler();
                        }
                    } else {
                        auto connected_handler = lockedMemberCopy(connected_handler_);
                        if(connected_handler) {
                            connected_handler();
                        }
                    }
                    break;
                }
                // Inform message handler
                std::function<void(const std::vector<nlohmann::json>&)> msg_handler;
                {
                    std::lock_guard<std::mutex> lock(event_handler_mutex_);
                    msg_handler = msg_handler_;
                }
                if(msg_handler) {
                    msg_handler(data);
                }
                // Inform event handler, if any is registered
                std::function<void(const nlohmann::json&)> event_handler;
                {
                    std::lock_guard<std::mutex> lock(event_handler_mutex_);
                    if(event_handlers_.count(event) > 0) {
                        event_handler = event_handlers_[event];
                    }
                }
                if(event_handler) {
                    nlohmann::json payload;
                    if(data.size() > 1) {
                        payload = data[1];
                    }
                    event_handler(payload);
                }
            }
            else {
                throw json_message_error("expected 'data' to be a non-empty array!");
            }
            break;
        }
        case PacketType::ACK: {
            // type === PacketType::ACK
            // We have to call the function
            auto id = get_json<int>(j, "id");
            Callback callback;
            {
                std::lock_guard<std::mutex> lock(ack_mutex_);
                if(acks_.count(id) > 0) {
                    callback = acks_[id];
                    acks_.erase(id);
                }
            }
            if(callback) {
                callback(j["data"].get<std::vector<nlohmann::json>>());
            }
            break;
        }
        default: {
            spdlog::warn("Unknown packet type received: {}", type);
            break;
        }
        }
    }
    catch (nlohmann::json::parse_error& e) {
        spdlog::error("Could not parse message from server as JSON: {}", e.what());
    }
    catch (nlohmann::json::exception& e) {
        spdlog::error("Error accessing data in json: {}", e.what());
    }
    catch(std::exception& err) {
        // TODO: Discuss error handling here
        spdlog::error("Could not parse message from server as JSON: {}",  err.what());
    }
}

void teckos::client::setMessageHandler(const std::function<void(const std::vector<nlohmann::json>&)>& handler) noexcept
{
    std::lock_guard<std::mutex> lock(event_handler_mutex_);
    msg_handler_ = handler;
}

void teckos::client::send(const std::string& event)
{
    if(!isConnected() && event != "token") {
        throw not_connected_exception();
    }
    sendPackage({PacketType::EVENT, {event, {}}, std::nullopt});
}

void teckos::client::send(const std::string& event, const nlohmann::json& args)
{
    if(!isConnected() && event != "token") {
        throw not_connected_exception();
    }
    sendPackage({PacketType::EVENT, {event, args}, std::nullopt});
}

void teckos::client::send(const std::string& event, const nlohmann::json& args, Callback callback)
{
    if(!isConnected() && event != "token") {
        throw not_connected_exception();
    }
    {
        // Record the callback for later use when the answer comes
        std::lock_guard<std::mutex> lock(ack_mutex_);
        acks_.insert({fn_id_, callback});
    }
    sendPackage({PacketType::EVENT, {event, args}, fn_id_++});
}

[[maybe_unused]] void teckos::client::send_json(const nlohmann::json& args)
{
    sendPackage({PacketType::EVENT, args, std::nullopt});
}

void teckos::client::sendPackage(teckos::packet packet)
{
    nlohmann::json json_msg = {{"type", static_cast<int>(packet.type)}, {"data", packet.data}};
    if(packet.number.has_value()) {
        json_msg["id"] = *packet.number;
    }
#ifdef DEBUG_TECKOS_SEND
    spdlog::debug("teckos:send >> {}", json_msg.dump());
#endif
#ifdef USE_IX_WEBSOCKET
    ws_->send(json_msg.dump());
#else
    web::websockets::client::websocket_outgoing_message msg;
    msg.set_utf8_message(json_msg.dump());
    try {
        std::shared_ptr<WebSocketClient> websocket;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            websocket = websocket_;
        }
        if(websocket) {
            websocket->send(msg).get();
        } else {
            assert(false);
            spdlog::error("Could not send message, websocket pointer is null");
        }
    }
    catch(std::exception& err) {
        // Usually an exception is thrown here, when the connection has been
        // disconnected in the meantime (since sendXY has been called)
        spdlog::warn("Could not send message, reason: {}", err.what());
    }
#endif
}

void teckos::client::setReconnectTrySleep(std::chrono::milliseconds milliseconds) noexcept
{
    timeout_ = milliseconds;
}

[[maybe_unused]] std::chrono::milliseconds teckos::client::reconnectTrySleep() const noexcept
{
    return timeout_;
}

bool teckos::client::isConnected() const noexcept
{
    return connected_ && authenticated_;
}

void teckos::client::setShouldReconnect(bool shallReconnect) noexcept
{
    settings_.reconnect = shallReconnect;
}

bool teckos::client::shouldReconnect() const noexcept
{
    return settings_.reconnect;
}

void teckos::client::setSendPayloadOnReconnect(bool sendPayloadOnReconnect) noexcept
{
    settings_.sendPayloadOnReconnect = sendPayloadOnReconnect;
}

[[maybe_unused]] bool teckos::client::isSendingPayloadOnReconnect() const noexcept
{
    return settings_.sendPayloadOnReconnect;
}

void teckos::client::on_connected(const std::function<void()>& handler) noexcept
{
    std::lock_guard<std::mutex> lock(event_handler_mutex_);
    connected_handler_ = handler;
}

void teckos::client::on_reconnected(const std::function<void()>& handler) noexcept
{
    std::lock_guard<std::mutex> lock(event_handler_mutex_);
    reconnected_handler_ = handler;
}

void teckos::client::on_disconnected(const std::function<void(bool)>& handler) noexcept
{
    std::lock_guard<std::mutex> lock(event_handler_mutex_);
    disconnected_handler_ = handler;
}

#ifndef USE_IX_WEBSOCKET
void teckos::client::startReconnecting()
{
    stopReconnecting();
    reconnecting_ = true;
    reconnection_thread_ = std::thread([this] {
        int reconnect_tries = 0;
        while(!connected_ && reconnecting_ && reconnect_tries < 10) {
            try {
                spdlog::debug("Reconnect ({}/{})", reconnect_tries, 10);
                connect();
            }
            catch(ConnectionException& e) {
                spdlog::warn("Connection closed during reconnect [code {}]: {}", e.code(), e.reason());
                connected_ = false;
                authenticated_ = false;
            }
            catch(...) {
                spdlog::error("Caught unknown exception in startReconnecting(), unhandled error!");
                auto disconnected_handler = lockedMemberCopy(disconnected_handler_);
                if(disconnected_handler) {
                    disconnected_handler(false);
                }
            }

            reconnect_tries++;
            std::this_thread::sleep_for(timeout_.load());
        }
        if(!connected_ && reconnecting_ && reconnect_tries >= 10) {
            // Reconnect tries exhausted, inform client
            auto disconnected_handler = lockedMemberCopy(disconnected_handler_);
            if(disconnected_handler) {
                disconnected_handler(false);
            }
        }
        reconnecting_ = false;
    });
}

void teckos::client::stopReconnecting()
{
    reconnecting_ = false;
    if(reconnection_thread_.joinable()) {
        reconnection_thread_.join();
    }
}
#endif
