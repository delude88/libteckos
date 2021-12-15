#include "teckos/global.h"
#include "teckos/client.h"
#include <memory>
#include <iostream>

teckos::client::client(bool use_async_events) noexcept:
    reconnecting(false),
    connected(false),
    async_events(use_async_events) {
  teckos::global::init();
#ifdef USE_IX_WEBSOCKET
  ix::initNetSystem();
  ws = std::make_shared<WebSocketClient>();
  ws->enablePong();
  ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
    switch (msg->type) {
      case ix::WebSocketMessageType::Message: {
        if (connected) {
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
        if (info.hasJwt) {
          // Not connected without the jwt being validated
          nlohmann::json p = info.payload;
          p["token"] = info.jwt;
          return this->send("token", p);
        } else {
          authenticated = true;
          // Without jwt we are connected now
          if (reconnecting) {
            if (reconnectedHandler) {
              if (async_events) {
                threadPool.emplace_back([this]() {
                  reconnectedHandler();
                });
              } else {
                reconnectedHandler();
              }
            }
          } else {
            if (connectedHandler) {
              if (async_events) {
                threadPool.emplace_back([this]() {
                  connectedHandler();
                });
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
      case ix::WebSocketMessageType::Ping:break;
      case ix::WebSocketMessageType::Pong:break;
      case ix::WebSocketMessageType::Fragment:break;
    }
  });
#else
  // Create new websocket client and attach handler
  ws = std::make_shared<WebSocketClient>();
  ws->set_message_handler([&](const websocket_incoming_message &ret_msg) {
    auto msg = ret_msg.extract_string().get();
    handleMessage(msg);
  });
  ws->set_close_handler([&](websocket_close_status close_status,
                            const utility::string_t &reason,
                            const std::error_code &/*error*/) {
    handleClose((int) close_status, utility::conversions::to_utf8string(reason));
  });
#endif
}

teckos::client::~client() {
  for (auto &item: threadPool) {
    if (item.joinable())
      item.join();
  }
  disconnect();
}

void teckos::client::on(
    const std::string &event,
    const std::function<void(const nlohmann::json &)> &handler) {
  // TODO: Support multiple handlers for a single event
  eventHandlers[event] = handler;
}

void teckos::client::off(const std::string &event) {
  eventHandlers.erase(event);
}

void teckos::client::connect(const std::string &url) noexcept(false) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  info = {url, false, "", ""};
  if (connected) {
    disconnect();
  }
  connect();
}

void teckos::client::connect(
    const std::string &url, const std::string &jwt,
    const nlohmann::json &initialPayload) noexcept(false) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  info = {url, true, jwt, initialPayload};
  if (connected) {
    disconnect();
  }
  return connect();
}

#ifdef USE_IX_WEBSOCKET
void teckos::client::connect() {
  // Create new websocket client and attach handler
  ws->setUrl(info.url);
  // Connect
  if (settings.reconnect) {
    ws->enableAutomaticReconnection();
  } else {
    ws->disableAutomaticReconnection();
  }
  connected = false;
  authenticated = false;
  ws->start();
}
#else
void teckos::client::connect() {
  // Connect
  connected = false;
  authenticated = false;
  // We have to do this sync step per step,
  // since a destruction of this object while
  // connecting would lead to undefined behavior
  ws->connect(utility::conversions::to_string_t(info.url)).get();
  connected = true;
  if (connectedHandler) {
    if (async_events) {
      threadPool.emplace_back([this]() {
        connectedHandler();
      });
    } else {
      connectedHandler();
    }
  }
  if (info.hasJwt) {
    nlohmann::json p = info.payload;
    p["token"] = info.jwt;
    return this->send("token", p);
  }
}
#endif

void teckos::client::disconnect() noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  try {
    if (connected) {
#ifdef USE_IX_WEBSOCKET
      ws->stop(1000, "Normal Closure");
#else
      ws->close(websocket_close_status::normal);
#endif
    }
    connected = false;
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << std::endl;
  }
}

void teckos::client::handleClose(int code, const std::string &/*reason*/) {
  connected = false;
  authenticated = false;
  auto abnormal_exit = code != 1000;
  if (abnormal_exit && settings.reconnect) {
    reconnecting = true;
#ifndef USE_IX_WEBSOCKET
    connect();
#endif
  }
  if (disconnectedHandler) {
    if (async_events) {
      threadPool.emplace_back([this, abnormal_exit]() {
        disconnectedHandler(std::move(abnormal_exit));
      });
    } else {
      disconnectedHandler(abnormal_exit);
    }
  }
}

void teckos::client::handleMessage(const std::string &msg) {
  if (msg == "hey")
    return;
  nlohmann::json j = nlohmann::json::parse(msg);
  const PacketType type = j["type"];
  switch (type) {
    case PacketType::EVENT: {
      const nlohmann::json jsonData = j["data"];
      if (jsonData.is_array() && !jsonData.empty()) {
        std::vector<nlohmann::json> data = j["data"];
        const std::string event = data[0];
        if (event == "ready") {
          // ready is sent by server to inform that client is connected and the
          // token valid
          authenticated = true;
          if (reconnecting) {
            if (reconnectedHandler) {
              if (async_events) {
                threadPool.emplace_back([this]() {
                  reconnectedHandler();
                });
              } else {
                reconnectedHandler();
              }
            }
          } else {
            if (connectedHandler) {
              if (async_events) {
                threadPool.emplace_back([this]() {
                  connectedHandler();
                });
              } else {
                connectedHandler();
              }
            }
          }
          break;
        }
        // Inform message handler
        if (msgHandler) {
          // Spawn a thread handling the assigned callbacks
          if (async_events) {
            threadPool.emplace_back([this, data]() {
              msgHandler(std::move(data));
            });
          } else {
            msgHandler(data);
          }
        }
        // Inform event handler
        if (eventHandlers.count(event) > 0) {
          nlohmann::json payload;
          if (data.size() > 1)
            payload = data[1];
          if (async_events) {
            threadPool.emplace_back([this, &event, &payload]() {
              eventHandlers[event](payload);
            });
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
      if (acks.count(id) > 0) {
        acks[id](j["data"].get<std::vector<nlohmann::json >>());
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

void teckos::client::setMessageHandler(
    const std::function<void(const std::vector<nlohmann::json> &)> &
    handler) noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  auto func = [handler](const std::vector<nlohmann::json> &json){
    handler(json);
  };
  msgHandler = func;
}

void teckos::client::send(const std::string &event) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!connected && !authenticated && event != "token") {
    throw std::runtime_error("Not connected");
  }
  return sendPackage({PacketType::EVENT, {event, {}}, std::nullopt});
}

void teckos::client::send(const std::string &event, const nlohmann::json &args) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!connected && !authenticated && event != "token") {
    throw std::runtime_error("Not connected");
  }
  return sendPackage({PacketType::EVENT, {event, args}, std::nullopt});
}

void teckos::client::send(
    const std::string &event, const nlohmann::json &args,
    Callback callback) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!connected && !authenticated && event != "token") {
    throw std::runtime_error("Not connected");
  }
  acks.insert({fnId, std::move(callback)});
  return sendPackage({PacketType::EVENT, {event, args}, fnId++});
}

void teckos::client::send_json(const nlohmann::json &args) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, args, std::nullopt});
}

void teckos::client::sendPackage(teckos::packet p) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  nlohmann::json jsonMsg = {{"type", p.type}, {"data", p.data}};
  if (p.number) {
    jsonMsg["id"] = *p.number;
  }
#ifdef USE_IX_WEBSOCKET
  ws->send(jsonMsg.dump());
#else
  websocket_outgoing_message msg;
  msg.set_utf8_message(jsonMsg.dump());
  ws->send(msg);
#endif
}

void teckos::client::setTimeout(std::chrono::milliseconds ms) noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  timeout = ms;
}

[[maybe_unused]] std::chrono::milliseconds
teckos::client::getTimeout() const noexcept {
  return timeout;
}

bool teckos::client::isConnected() const noexcept {
  return connected && authenticated;
}

void teckos::client::setReconnect(bool shallReconnect) noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  settings.reconnect = shallReconnect;
}

bool teckos::client::shouldReconnect() const noexcept {
  return settings.reconnect;
}

void teckos::client::sendPayloadOnReconnect(
    bool sendPayloadOnReconnect) noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  settings.sendPayloadOnReconnect = sendPayloadOnReconnect;
}

[[maybe_unused]] bool
teckos::client::isSendingPayloadOnReconnect() const noexcept {
  return settings.sendPayloadOnReconnect;
}

void teckos::client::on_connected(const std::function<void()> &handler) noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  connectedHandler = handler;
}

void teckos::client::on_reconnected(
    const std::function<void()> &handler) noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  auto func = [handler](){
    handler();
  };
  reconnectedHandler = func;
}

void teckos::client::on_disconnected(
    const std::function<void(bool)> &handler) noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  auto func = [handler](bool result){
    handler(result);
  };
  disconnectedHandler = func;
}
