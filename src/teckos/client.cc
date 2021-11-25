#include "teckos/client.h"
#include <memory>
#include <iostream>
#include <future>

teckos::client::client() noexcept: reconnecting(false), connected(false) {
#ifdef TECKOS_TRACE
  std::cout << "teckos::client::client()" << std::endl;
#endif
  ix::initNetSystem();
  ws.reset(new ix::WebSocket());
  ws->enablePong();
}

teckos::client::~client() {
#ifdef TECKOS_TRACE
  std::cout << "teckos::client::~client()" << std::endl;
#endif
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
#ifdef TECKOS_TRACE
  std::cout << "teckos::client::connect(const std::string& url)" << std::endl;
#endif
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
#ifdef TECKOS_TRACE
  std::cout << "teckos::client::connect(const std::string& url, const "
               "std::string& jwt, const nlohmann::json& initialPayload)"
            << std::endl;
#endif
  std::lock_guard<std::recursive_mutex> lock(mutex);
  info = {url, true, jwt, initialPayload};
  if (connected) {
    disconnect();
  }
  return connect();
}

void teckos::client::connect() {
#ifdef TECKOS_TRACE
  std::cout << "teckos::client::connect() to " << info.url << std::endl;
  std::cout << info.url << std::endl;
#endif
  // Create new websocket client and attach handler
  ws->setUrl(info.url);

  ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
    switch (msg->type) {
      case ix::WebSocketMessageType::Message: {
        if (connected) {
#ifdef TECKOS_TRACE
          std::cout << "teckos::client::setOnMessageCallback(Message)" << std::endl;
#endif
          handleMessage(msg);
        }
        break;
      }
      case ix::WebSocketMessageType::Close: {
#ifdef TECKOS_TRACE
        std::cout << "teckos::client::setOnMessageCallback(Close)" << std::endl;
#endif
        handleClose(msg);
        break;
      }
      case ix::WebSocketMessageType::Open: {
#ifdef TECKOS_TRACE
        std::cout << "teckos::client::setOnMessageCallback(Open)" << std::endl;
#endif
        connected = true;
        if (reconnecting && reconnectedHandler) {
          reconnectedHandler();
        } else if (connectedHandler) {
          connectedHandler();
        }
        reconnecting = false;
        if (info.hasJwt) {
          nlohmann::json p = info.payload;
          p["token"] = info.jwt;
          return this->send("token", p);
        }
        break;
      }
      case ix::WebSocketMessageType::Error: {
#ifdef TECKOS_TRACE
        std::cout << "teckos::client::setOnMessageCallback(Error)" << std::endl;
#endif
        std::cerr << msg->errorInfo.reason << std::endl;
        break;
      }
      case ix::WebSocketMessageType::Ping:break;
      case ix::WebSocketMessageType::Pong:break;
      case ix::WebSocketMessageType::Fragment:break;
    }
  });
  // Connect
  //TODO: Prefer automatic reconnection of ixwebsocket
  ws->disableAutomaticReconnection();
  ws->start();
}

void teckos::client::disconnect() noexcept {
#ifdef TECKOS_TRACE
  std::cout << "teckos::client::disconnect" << std::endl;
#endif
  std::lock_guard<std::recursive_mutex> lock(mutex);
  connected = false;
  try {
    ws->stop(1000, "Normal Closure");
  } catch (std::exception exception) {
    std::cerr << exception.what() << std::endl;
  }
}

void teckos::client::handleClose(const ix::WebSocketMessagePtr &msg) {
#ifdef TECKOS_TRACE
  std::cout << "teckos::client::handleClose" << std::endl;
#endif

  if (msg->closeInfo.code != 1000) {
    // Abnormal close, reconnect may be appropriate
    if (disconnectedHandler) {
#ifdef TECKOS_TRACE
      std::cout << "teckos::client::handleClose::disconnectedHandler(false)" << std::endl;
#endif
      //disconnectedHandler(false);
      std::thread([=]() { disconnectedHandler(false); }).detach();
      //std::async(std::launch::deferred, [=] { disconnectedHandler(false); });
    }
    if (settings.reconnect) {
#ifdef TECKOS_TRACE
      std::cout << "teckos::client::handleClose::reconneting..." << std::endl;#
#endif
      reconnecting = true;
      if (reconnectingHandler) {
        //reconnectingHandler();
        std::thread([=]() { reconnectingHandler(); }).detach();
        //std::async(std::launch::deferred, [=] { reconnectingHandler(); });
      }
      if (info.hasJwt) {
        if (settings.sendPayloadOnReconnect) {
          connect(info.url, info.jwt, info.payload);
        } else {
          connect(info.url, info.jwt, {});
        }
      } else {
        connect(info.url);
      }
    }
  } else {
    if (disconnectedHandler) {
#ifdef TECKOS_TRACE
      std::cout << "teckos::client::handleClose::disconnectedHandler(true)" << std::endl;
#endif
      //disconnectedHandler(true);
      std::thread([=]() { disconnectedHandler(true); }).detach();
      //std::async(std::launch::deferred, [=] { disconnectedHandler(true); });
    }
  }
}

void teckos::client::handleMessage(const ix::WebSocketMessagePtr &msg) {
  auto ret_str = msg->str;
  if (ret_str == "hey")
    return;
  nlohmann::json j = nlohmann::json::parse(ret_str);
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
          break;
        }
        // Inform message handler
        if (msgHandler) {
          // Spawn a thread handling the assigned callbacks
          //msgHandler(data);
          std::thread([=]() { msgHandler(data); }).detach();
          //std::async(std::launch::deferred, [=] { msgHandler(data); });
        }
        // Inform event handler
        if (eventHandlers.count(event) > 0) {
          nlohmann::json payload;
          if (data.size() > 1)
            payload = data[1];
          //eventHandlers[event](payload);
          std::thread([=]() { eventHandlers[event](payload); }).detach();
          //std::async(std::launch::deferred, [=] { eventHandlers[event](payload); });
        }
      }
      break;
    }
    case PacketType::ACK: {
      // type === PacketType::ACK
      // We have to call the function
      const int32_t id = j["id"];
      if (acks.count(id) > 0) {
        acks.at(id)(j["data"].get<std::vector<nlohmann::json>>());
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
  msgHandler = handler;
}

void teckos::client::send(const std::string &event) {
#ifdef TECKOS_TRACE
  std::cout << "teckos::client::send(" << event << ")" << std::endl;
#endif
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, {event, {}}, std::nullopt});
}

void teckos::client::send(const std::string &event, const nlohmann::json &args) {
#ifdef TECKOS_TRACE
  std::cout << "teckos::client::send(" << event << ", " << args.dump() << ")" << std::endl;
#endif
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, {event, args}, std::nullopt});
}

void teckos::client::send(
    const std::string &event, const nlohmann::json &args,
    const std::function<void(const std::vector<nlohmann::json> &)> &callback) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  acks[fnId] = callback;
  return sendPackage({PacketType::EVENT, {event, args}, fnId++});
}

void teckos::client::send_json(const nlohmann::json &args) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, args, std::nullopt});
}

void teckos::client::sendPackage(teckos::packet p) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!connected) {
    throw std::runtime_error("Not connected");
  }
  nlohmann::json jsonMsg = {{"type", p.type}, {"data", p.data}};
  if (p.number) {
    jsonMsg["id"] = *p.number;
  }
#ifdef TECKOS_TRACE
  std::cout << "ws->send(" << jsonMsg.dump() << ")" << std::endl;
#endif
  ws->send(jsonMsg.dump());
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
  return connected;
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
  reconnectedHandler = handler;
}

void teckos::client::on_reconnecting(
    const std::function<void()> &handler) noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  reconnectingHandler = handler;
}

void teckos::client::on_disconnected(
    const std::function<void(bool)> &handler) noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  disconnectedHandler = handler;
}
