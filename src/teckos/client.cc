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

teckos::client::client(bool use_async_events) noexcept
    : reconnecting(false), connected(false), authenticated(false),
      use_async_events(use_async_events)
{
  teckos::global::init();
#ifdef USE_IX_WEBSOCKET
  ix::initNetSystem();
  ws = std::make_unique<WebSocketClient>();
  ws->enablePong();
  ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    switch(msg->type) {
    case ix::WebSocketMessageType::Message: {
      if(connected) {
        handleMessage(msg->str);
      }
      break;
    }
    case ix::WebSocketMessageType::Close: {
      handleClose(msg->closeInfo.code, msg->closeInfo.reason);
      break;
    }
    case ix::WebSocketMessageType::Open: {
      connected = true;
      if(info.hasJwt) {
        // Not connected without the jwt being validated
        nlohmann::json p = info.payload;
        p["token"] = info.jwt;
        return this->send("token", p);
      } else {
        authenticated = true;
        // Without jwt we are connected now
        if(reconnecting) {
          if(reconnectedHandler) {
            if(async_events) {
              threadPool.emplace_back([this]() { reconnectedHandler(); });
            } else {
              reconnectedHandler();
            }
          }
        } else {
          if(connectedHandler) {
            if(async_events) {
              threadPool.emplace_back([this]() { connectedHandler(); });
            } else {
              connectedHandler();
            }
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
  for(auto& item : event_handler_thread_pool_) {
    if(item.joinable()) {
      item.join();
    }
  }
  disconnect();
}

void teckos::client::on(
    const std::string& event,
    const std::function<void(const nlohmann::json&)>& handler)
{
  // TODO: Support multiple handlers for a single event
  eventHandlers[event] = handler;
}

void teckos::client::off(const std::string& event)
{
  eventHandlers.erase(event);
}

void teckos::client::connect(const std::string& url) noexcept(false)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  info = {url, false, "", ""};
  if(connected) {
    disconnect();
  }
  was_connected_before_ = false;
  connect();
}

void teckos::client::connect(
    const std::string& url, const std::string& jwt,
    const nlohmann::json& initialPayload) noexcept(false)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  info = {url, true, jwt, initialPayload};
  if(connected) {
    disconnect();
  }
  was_connected_before_ = false;
  connect();
}

#ifdef USE_IX_WEBSOCKET
void teckos::client::connect()
{
  // Create new websocket client and attach handler
  ws->setUrl(info.url);
  // Connect
  if(settings.reconnect) {
    ws->enableAutomaticReconnection();
  } else {
    ws->disableAutomaticReconnection();
  }
  connected = false;
  authenticated = false;
  ws->start();
}
#else
void teckos::client::connect()
{
  // Connect
  connected = false;
  authenticated = false;
  ws = std::make_unique<WebSocketClient>();
  ws->set_message_handler(
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
  ws->set_close_handler(
      [this](web::websockets::client::websocket_close_status close_status,
             const utility::string_t& reason,
             const std::error_code& code) {
      connected = false;
      if (close_status == web::websockets::client::websocket_close_status::abnormal_close) {
        if(!reconnecting && was_connected_before_) {
          // Start reconnecting
          startReconnecting();
        }
      } else {
        // Graceful disconnect, inform handler (if any)
        if(disconnectedHandler) {
          if(use_async_events) {
            event_handler_thread_pool_.emplace_back([this]() { disconnectedHandler(true); });
          } else {
            disconnectedHandler(true);
          }
        }
      }
      });
  try {
    ws->connect(utility::conversions::to_string_t(info.url)).wait();
    connected = true;
    was_connected_before_ = true;
    if(connectedHandler) {
      if(use_async_events) {
        event_handler_thread_pool_.emplace_back([this]() { connectedHandler(); });
      } else {
        connectedHandler();
      }
    }
    if(info.hasJwt) {
      nlohmann::json payload = info.payload;
      payload["token"] = info.jwt;
      this->send("token", payload);
    }
  }
  catch(web::websockets::client::websocket_exception& e) {
    connected = false;
    throw ConnectionException(e.error_code().value(), e.what());
  }
}
#endif

void teckos::client::disconnect()
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
#ifndef USE_IX_WEBSOCKET
  stopReconnecting();
#endif
  if(connected) {
#ifdef USE_IX_WEBSOCKET
    ws->stop(1000, "Normal Closure");
#else
    // Now close connecting, if it is still there
    if(ws) {
      ws->close(web::websockets::client::websocket_close_status::normal).wait();
    }
#endif
  }
  connected = false;
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
          authenticated = true;
          if(reconnecting) {
            if(reconnectedHandler) {
              if(use_async_events) {
                event_handler_thread_pool_.emplace_back([this]() { reconnectedHandler(); });
              } else {
                reconnectedHandler();
              }
            }
          } else {
            if(connectedHandler) {
              if(use_async_events) {
                event_handler_thread_pool_.emplace_back([this]() { connectedHandler(); });
              } else {
                connectedHandler();
              }
            }
          }
          break;
        }
        // Inform message handler
        if(msgHandler) {
          // Spawn a thread handling the assigned callbacks
          if(use_async_events) {
            event_handler_thread_pool_.emplace_back(
                [this, data]() { msgHandler(std::move(data)); });
          } else {
            msgHandler(data);
          }
        }
        // Inform event handler
        if(eventHandlers.count(event) > 0) {
          nlohmann::json payload;
          if(data.size() > 1)
            payload = data[1];
          if(use_async_events) {
            event_handler_thread_pool_.emplace_back(
                [this, &event, &payload]() { eventHandlers[event](payload); });
          } else {
            eventHandlers[event](payload);
          }
        }
      }
      break;
    }
    case PacketType::ACK: {
      // type === PacketType::ACK
      // We have to call the function
      const int32_t id = j["id"];
      if(acks.count(id) > 0) {
        acks[id](j["data"].get<std::vector<nlohmann::json>>());
        acks.erase(id);
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
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(handler) {
    auto func = [handler](const std::vector<nlohmann::json>& json) {
      handler(json);
    };
    msgHandler = func;
  } else {
    msgHandler = nullptr;
  }
}

void teckos::client::send(const std::string& event)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!connected && !authenticated && event != "token") {
    throw std::runtime_error("Not connected");
  }
  return sendPackage({PacketType::EVENT, {event, {}}, std::nullopt});
}

void teckos::client::send(const std::string& event, const nlohmann::json& args)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!connected && !authenticated && event != "token") {
    throw std::runtime_error("Not connected");
  }
  return sendPackage({PacketType::EVENT, {event, args}, std::nullopt});
}

void teckos::client::send(const std::string& event, const nlohmann::json& args,
                          Callback callback)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!connected && !authenticated && event != "token") {
    throw std::runtime_error("Not connected");
  }
  acks.insert({fnId, std::move(callback)});
  return sendPackage({PacketType::EVENT, {event, args}, fnId++});
}

[[maybe_unused]] void teckos::client::send_json(const nlohmann::json& args)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, args, std::nullopt});
}

void teckos::client::sendPackage(teckos::packet p)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  nlohmann::json jsonMsg = {{"type", p.type}, {"data", p.data}};
  if(p.number) {
    jsonMsg["id"] = *p.number;
  }
#ifdef DEBUG_TECKOS_SEND
  std::cout << "teckos:send >> " << jsonMsg.dump() << std::endl;
#endif
#ifdef USE_IX_WEBSOCKET
  ws->send(jsonMsg.dump());
#else
  web::websockets::client::websocket_outgoing_message msg;
  msg.set_utf8_message(jsonMsg.dump());
  try {
    ws->send(msg).get();
  }
  catch(std::exception& err) {
    // Usually an exception is thrown here, when the connection has been
    // disconnected in the meantime (since sendXY has been called)
    std::cerr << "Warning: could not send message, reason: " << err.what()
              << std::endl;
  }
#endif
}

void teckos::client::setTimeout(std::chrono::milliseconds ms) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  timeout = ms;
}

[[maybe_unused]] std::chrono::milliseconds
teckos::client::getTimeout() const noexcept
{
  return timeout;
}

bool teckos::client::isConnected() const noexcept
{
  return connected && authenticated;
}

void teckos::client::setReconnect(bool shallReconnect) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  settings.reconnect = shallReconnect;
}

bool teckos::client::shouldReconnect() const noexcept
{
  return settings.reconnect;
}

void teckos::client::sendPayloadOnReconnect(
    bool sendPayloadOnReconnect) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  settings.sendPayloadOnReconnect = sendPayloadOnReconnect;
}

[[maybe_unused]] bool
teckos::client::isSendingPayloadOnReconnect() const noexcept
{
  return settings.sendPayloadOnReconnect;
}

void teckos::client::on_connected(const std::function<void()>& handler) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  connectedHandler = handler;
}

void teckos::client::on_reconnected(
    const std::function<void()>& handler) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(handler) {
    auto func = [handler]() { handler(); };
    reconnectedHandler = func;
  } else {
    reconnectedHandler = nullptr;
  }
}

void teckos::client::on_disconnected(
    const std::function<void(bool)>& handler) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(handler) {
    auto func = [handler](bool result) { handler(result); };
    disconnectedHandler = func;
  } else {
    disconnectedHandler = nullptr;
  }
}

#ifndef USE_IX_WEBSOCKET
void teckos::client::startReconnecting()
{
  stopReconnecting();
  reconnecting = true;
  reconnectionThread = std::thread([this] {
    int reconnectTries = 0;
    while(!connected && reconnecting && reconnectTries < 10) {
      try {
          std::cout << "Reconnect (" << reconnectTries << "/" << 10 << ")" << std::endl;
          connect();
      }
      catch (ConnectionException& e) {
          std::cout << "Connection closed by [" << e.code() << "]: " << e.reason() << std::endl;
          connected = false;
          authenticated = false;
      }
      catch (...) {
          if (disconnectedHandler) {
              if (use_async_events) {
                  event_handler_thread_pool_.emplace_back(
                      [this]() { disconnectedHandler(false); });
              }
              else {
                  disconnectedHandler(false);
              }
          }
      }

      reconnectTries++;
      std::this_thread::sleep_for(timeout);
    }
    if(!connected && reconnecting && reconnectTries >= 10) {
      // Reconnect tries exhausted, inform client
      if (disconnectedHandler) {
        if (use_async_events) {
          event_handler_thread_pool_.emplace_back(
              [this]() { disconnectedHandler(false); });
        }
        else {
          disconnectedHandler(false);
        }
      }
    }
    reconnecting = false;
  });
}

void teckos::client::stopReconnecting() {
  reconnecting = false;
  if(reconnectionThread.joinable()) {
    reconnectionThread.join();
  }
}
#endif
