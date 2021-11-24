#include "teckos/client.h"
#include <memory>

teckos::client::client() noexcept : reconnect(false), connected(false)
{
  ix::initNetSystem();
}

teckos::client::~client()
{
  reconnect = false;
  if(reconnectionThread) {
    reconnectionThread->join();
  }
  if(connected) {
    disconnect();
  }
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
  std::cout << "teckos::client::connect(const std::string& url)" << std::endl;
  std::lock_guard<std::recursive_mutex> lock(mutex);
  info = {url, false, "", ""};
  if(connected) {
    disconnect();
  }
  connect();
}

void teckos::client::connect(
    const std::string& url, const std::string& jwt,
    const nlohmann::json& initialPayload) noexcept(false)
{
  std::cout << "teckos::client::connect(const std::string& url, const "
               "std::string& jwt, const nlohmann::json& initialPayload)"
            << std::endl;
  std::lock_guard<std::recursive_mutex> lock(mutex);
  info = {url, true, jwt, initialPayload};
  if(connected) {
    disconnect();
  }
  return connect();
}

void teckos::client::connect()
{
  std::cout << "teckos::client::connect()" << std::endl;
  // Create new websocket client and attach handler
  ws.reset(new ix::WebSocket());
  ws->enablePong();
  ws->setUrl(info.url);
  std::cout << info.url << std::endl;

  ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    switch(msg->type) {
    case ix::WebSocketMessageType::Message:
      handleMessage(msg);
      break;
    case ix::WebSocketMessageType::Close:
      handleClose(msg);
      break;
    case ix::WebSocketMessageType::Open:
      reconnect = false;
      connected = true;
      if(connectedHandler) {
        connectedHandler();
      }
      if(info.hasJwt) {
        nlohmann::json p = info.payload;
        p["token"] = info.jwt;
        return this->send("token", p);
      }
      break;
    case ix::WebSocketMessageType::Error:
      std::cerr << msg->errorInfo.reason << std::endl;
      break;
    case ix::WebSocketMessageType::Ping:
      break;
    case ix::WebSocketMessageType::Pong:
      break;
    case ix::WebSocketMessageType::Fragment:
      break;
    }
  });
  // Connect
  ws->start();
}

void teckos::client::disconnect() noexcept
{
  std::cout << "teckos::client::disconnect" << std::endl;
  std::lock_guard<std::recursive_mutex> lock(mutex);
  reconnect = false;
  if(ws) {
    return ws->close(1000, "Normal Closure");
  }
}

void teckos::client::handleClose(const ix::WebSocketMessagePtr& msg)
{
  connected = false;

  if(msg->closeInfo.code != 1000) {
    // Abnormal close, reconnect may be appropriate
    if(disconnectedHandler) {
      disconnectedHandler(false);
    }
    if(settings.reconnect && !reconnect) {
      reconnect = true;
      // Wait for old reconnect
      if(reconnectionThread) {
        reconnectionThread->join();
      }
      reconnectionThread.reset(
          new std::thread(&teckos::client::reconnectionService, this));
    }
  } else {
    if(disconnectedHandler) {
      disconnectedHandler(true);
    }
  }
}

void teckos::client::reconnectionService()
{
  std::this_thread::sleep_for(timeout);
  while(reconnect && !connected && settings.reconnect) {
    if(reconnectingHandler) {
      reconnectingHandler();
    }
    try {
      if(info.hasJwt) {
        if(settings.sendPayloadOnReconnect) {
          // TODO: Do we need to wait for the connect?
          connect(info.url, info.jwt, info.payload);
        } else {
          connect(info.url, info.jwt, {});
        }
      } else {
        connect(info.url);
      }
      if(reconnectedHandler) {
        reconnectedHandler();
      }
    }
    catch(...) {
      std::this_thread::sleep_for(timeout);
    }
  }
}

void teckos::client::handleMessage(const ix::WebSocketMessagePtr& msg)
{
  auto ret_str = msg->str;
  if(ret_str == "hey")
    return;
  nlohmann::json j = nlohmann::json::parse(ret_str);
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
        break;
      }
      // Inform message handler
      if(msgHandler) {
        // Spawn a thread handling the assigned callbacks
        /* TODO: Discuss, if we should kill all threads when the destructor is
         * called or the connection gets lost */
        std::thread([=]() { msgHandler(data); }).detach();
      }
      // Inform event handler
      if(eventHandlers.count(event) > 0) {
        nlohmann::json payload;
        if(data.size() > 1)
          payload = data[1];
        /* TODO: Discuss, if we should kill all threads when the destructor is
         * called or the connection gets lost */
        std::thread([=]() { eventHandlers[event](payload); }).detach();
      }
    }
    break;
  }
  case PacketType::ACK: {
    // type === PacketType::ACK
    // We have to call the function
    const int32_t id = j["id"];
    if(acks.count(id) > 0) {
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
    const std::function<void(const std::vector<nlohmann::json>&)>&
        handler) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  msgHandler = handler;
}

void teckos::client::send(const std::string& event)
{
  std::cout << "teckos::client::send(" << event << ")" << std::endl;
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, {event, {}}, nullopt});
}

void teckos::client::send(const std::string& event, const nlohmann::json& args)
{
  std::cout << "teckos::client::send(" << event << ", " << args.dump() << ")" << std::endl;
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, {event, args}, nullopt});
}

void teckos::client::send(
    const std::string& event, const nlohmann::json& args,
    const std::function<void(const std::vector<nlohmann::json>&)>& callback)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  acks[fnId] = callback;
  return sendPackage({PacketType::EVENT, {event, args}, fnId++});
}

void teckos::client::send_json(const nlohmann::json& args)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, args, nullopt});
}

void teckos::client::sendPackage(teckos::packet p)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!connected) {
    throw std::runtime_error("Not connected");
  }
  nlohmann::json jsonMsg = {{"type", p.type}, {"data", p.data}};
  if(p.number) {
    jsonMsg["id"] = *p.number;
  }
  std::cout << "ws->send(" << jsonMsg.dump() << ")" << std::endl;
  ws->send(jsonMsg.dump());
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
  return connected;
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
  reconnectedHandler = handler;
}

void teckos::client::on_reconnecting(
    const std::function<void()>& handler) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  reconnectingHandler = handler;
}

void teckos::client::on_disconnected(
    const std::function<void(bool)>& handler) noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  disconnectedHandler = handler;
}
