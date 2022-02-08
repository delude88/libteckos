#include "teckos/client.h"
#include "teckos/global.h"   // for global
#include <exception>         // for exception
#include <iostream>          // for string, operator<<
#include <mutex>             // for lock_guard
#include <nlohmann/json.hpp> // for json_ref
#include <utility>           // for move

class ConnectionException : public std::exception {
public:
    ConnectionException(int code, std::string const& reason) : code_(code), reason_(reason) {}

    int code() const { return code_; }
    std::string reason() const { return reason_;  }

private:
    int code_;
    std::string reason_;
};

teckos::client::client() 
    : was_connected_before_(false), reconnecting_(false), connected_(false),
      authenticated_(false), timeout_(std::chrono::milliseconds(500))
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
      if (disconnected_handler_) {
        disconnected_handler_(abnormal_exit);
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
          if(reconnected_handler_) {
            reconnected_handler_();
          }
        } else {
          if(connected_handler_) {
             connected_handler_();
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

void teckos::client::on(
    const std::string& event,
    const std::function<void(const nlohmann::json&)>& handler)
{
  // TODO: Support multiple handlers for a single event
  event_handlers_[event] = handler;
}

void teckos::client::off(const std::string& event)
{
  event_handlers_.erase(event);
}

void teckos::client::connect(const std::string& url) noexcept(false)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  info_ = {url, false, "", ""};
  if(connected_) {
    disconnect();
  }
  was_connected_before_ = false;
  connect();
}

void teckos::client::connect(
    const std::string& url, const std::string& jwt,
    const nlohmann::json& initialPayload) noexcept(false)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
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
  ws_ = std::make_unique<WebSocketClient>();
  ws_->set_message_handler(
      [this](
          const web::websockets::client::websocket_incoming_message& ret_msg) {
        try {
          auto msg = ret_msg.extract_string().get();
          handleMessage(msg);
        }
        catch(std::exception& err) {
          // TODO: Discuss error handling here
          std::cerr << "Invalid message from server: " << err.what()
                    << std::endl;
        }
        catch(...) {
          std::cerr << "Unhandled exception occurred when parsing incoming "
                       "message or calling handleMessage"
                    << std::endl;
        }
      });
  ws_->set_close_handler(
      [this](web::websockets::client::websocket_close_status close_status,
             const utility::string_t& reason,
             const std::error_code& code) {
        connected_ = false;
      if (close_status == web::websockets::client::websocket_close_status::abnormal_close) {
        if(!reconnecting_ && was_connected_before_) {
          // Start reconnecting
          startReconnecting();
        }
      } else {
        // Graceful disconnect, inform handler (if any)
        if(disconnected_handler_) {
          disconnected_handler_(true);
        }
      }
      });
  try {
    ws_->connect(utility::conversions::to_string_t(info_.url)).wait();
    connected_ = true;
    was_connected_before_ = true;
    if(connected_handler_) {
      connected_handler_();
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
  std::lock_guard<std::recursive_mutex> lock(mutex_);
#ifndef USE_IX_WEBSOCKET
  stopReconnecting();
#endif
  if(connected_) {
#ifdef USE_IX_WEBSOCKET
    ws_->stop(1000, "Normal Closure");
#else
    // Now close connecting, if it is still there
    if(ws_) {
      ws_->close(web::websockets::client::websocket_close_status::normal).wait();
    }
#endif
  }
  connected_ = false;
}

void teckos::client::handleMessage(const std::string& msg) noexcept
{
  if(msg == "hey")
    return;
#ifdef DEBUG_TECKOS_RECV
  std::cout << "teckos:receive << " << msg << std::endl;
#endif
  try {
    nlohmann::json j = nlohmann::json::parse(msg);
    const PacketType type = j["type"];
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
            if(reconnected_handler_) {
              reconnected_handler_();
            }
          } else {
            if(connected_handler_) {
              connected_handler_();
            }
          }
          break;
        }
        // Inform message handler
        if(msg_handler_) {
          msg_handler_(data);
        }
        // Inform event handler
        if(event_handlers_.count(event) > 0) {
          nlohmann::json payload;
          if(data.size() > 1)
            payload = data[1];
          event_handlers_[event](payload);
        }
      }
      break;
    }
    case PacketType::ACK: {
      // type === PacketType::ACK
      // We have to call the function
      const int32_t id = j["id"];
      if(acks_.count(id) > 0) {
        acks_[id](j["data"].get<std::vector<nlohmann::json>>());
        acks_.erase(id);
      }
      break;
    }
    default: {
      std::cerr << "Warning: unknown packet type received: " << std::endl;
      break;
    }
    }
  }
  catch(std::exception& err) {
    // TODO: Discuss error handling here
    std::cerr << "Could not parse message from server as JSON: " << err.what()
              << std::endl;
  }
}

void teckos::client::setMessageHandler(
    const std::function<void(const std::vector<nlohmann::json>&)>&
        handler) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(handler) {
    auto func = [handler](const std::vector<nlohmann::json>& json) {
      handler(json);
    };
    msg_handler_ = func;
  } else {
    msg_handler_ = nullptr;
  }
}

void teckos::client::send(const std::string& event)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!connected_ && !authenticated_ && event != "token") {
    throw std::runtime_error("Not connected");
  }
  return sendPackage({PacketType::EVENT, {event, {}}, std::nullopt});
}

void teckos::client::send(const std::string& event, const nlohmann::json& args)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!connected_ && !authenticated_ && event != "token") {
    throw std::runtime_error("Not connected");
  }
  return sendPackage({PacketType::EVENT, {event, args}, std::nullopt});
}

void teckos::client::send(const std::string& event, const nlohmann::json& args,
                          Callback callback)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!connected_ && !authenticated_ && event != "token") {
    throw std::runtime_error("Not connected");
  }
  acks_.insert({fn_id_, std::move(callback)});
  return sendPackage({PacketType::EVENT, {event, args}, fn_id_++});
}

[[maybe_unused]] void teckos::client::send_json(const nlohmann::json& args)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return sendPackage({PacketType::EVENT, args, std::nullopt});
}

void teckos::client::sendPackage(teckos::packet packet)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  nlohmann::json json_msg = {{"type", packet.type}, {"data", packet.data}};
  if(packet.number) {
    json_msg["id"] = *packet.number;
  }
#ifdef DEBUG_TECKOS_SEND
  std::cout << "teckos:send >> " << json_msg.dump() << std::endl;
#endif
#ifdef USE_IX_WEBSOCKET
  ws_->send(json_msg.dump());
#else
  web::websockets::client::websocket_outgoing_message msg;
  msg.set_utf8_message(json_msg.dump());
  try {
    ws_->send(msg).get();
  }
  catch(std::exception& err) {
    // Usually an exception is thrown here, when the connection has been
    // disconnected in the meantime (since sendXY has been called)
    std::cerr << "Warning: could not send message, reason: " << err.what()
              << std::endl;
  }
#endif
}

void teckos::client::setTimeout(std::chrono::milliseconds milliseconds) noexcept
{
  timeout_ = milliseconds;
}

[[maybe_unused]] std::chrono::milliseconds
teckos::client::getTimeout() const noexcept
{
  return timeout_;
}

bool teckos::client::isConnected() const noexcept
{
  return connected_ && authenticated_;
}

void teckos::client::setReconnect(bool shallReconnect) noexcept
{
  settings_.reconnect = shallReconnect;
}

bool teckos::client::shouldReconnect() const noexcept
{
  return settings_.reconnect;
}

void teckos::client::sendPayloadOnReconnect(
    bool sendPayloadOnReconnect) noexcept
{
  settings_.sendPayloadOnReconnect = sendPayloadOnReconnect;
}

[[maybe_unused]] bool
teckos::client::isSendingPayloadOnReconnect() const noexcept
{
  return settings_.sendPayloadOnReconnect;
}

void teckos::client::on_connected(const std::function<void()>& handler) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  connected_handler_ = handler;
}

void teckos::client::on_reconnected(
    const std::function<void()>& handler) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(handler) {
    auto func = [handler]() { handler(); };
    reconnected_handler_ = func;
  } else {
    reconnected_handler_ = nullptr;
  }
}

void teckos::client::on_disconnected(
    const std::function<void(bool)>& handler) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(handler) {
    auto func = [handler](bool result) { handler(result); };
    disconnected_handler_ = func;
  } else {
    disconnected_handler_ = nullptr;
  }
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
          std::cout << "Reconnect (" << reconnect_tries << "/" << 10 << ")" << std::endl;
          connect();
      }
      catch (ConnectionException& e) {
          std::cout << "Connection closed by [" << e.code() << "]: " << e.reason() << std::endl;
          connected_ = false;
          authenticated_ = false;
      }
      catch (...) {
          if (disconnected_handler_) {
            disconnected_handler_(false);
          }
      }

      reconnect_tries++;
      std::this_thread::sleep_for(timeout_.load());
    }
    if(!connected_ && reconnecting_ && reconnect_tries >= 10) {
      // Reconnect tries exhausted, inform client
      if (disconnected_handler_) {
        disconnected_handler_(false);
      }
    }
    reconnecting_ = false;
  });
}

void teckos::client::stopReconnecting() {
  reconnecting_ = false;
  if(reconnection_thread_.joinable()) {
    reconnection_thread_.join();
  }
}
#endif
