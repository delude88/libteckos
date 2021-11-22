#include "teckos/client.h"

#include <memory>

// https://stackoverflow.com/questions/215963/how-do-you-properly-use-widechartomultibyte
static std::string convert_to_utf8(const utility::string_t& potentiallywide)
{
#ifdef WIN32
  if(potentiallywide.empty())
    return std::string();
  int size_needed =
      WideCharToMultiByte(CP_UTF8, 0, &potentiallywide[0],
                          (int)potentiallywide.size(), NULL, 0, NULL, NULL);
  std::string strTo(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, &potentiallywide[0],
                      (int)potentiallywide.size(), &strTo[0], size_needed, NULL,
                      NULL);
  return strTo;
#else
  return potentiallywide;
#endif
}

teckos::client::client() noexcept : reconnect(false), connected(false) {}

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

pplx::task<void> teckos::client::connect(const string_t& url) noexcept(false)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  info = {url, false};
  if(connected) {
    return disconnect().then([&]() { return connect(); });
  }
  return connect();
}

pplx::task<void>
teckos::client::connect(const string_t& url, const string_t& jwt,
                        const nlohmann::json& initialPayload) noexcept(false)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  info = {url, true, jwt, initialPayload};
  if(connected) {
    return disconnect().then([&]() { return connect(); });
  }
  return connect();
}

pplx::task<void> teckos::client::connect()
{
  // Create new websocket client and attach handler
  ws = std::make_shared<websocket_callback_client>();
  ws->set_message_handler([&](const websocket_incoming_message& ret_msg) {
    handleMessage(ret_msg);
  });
  ws->set_close_handler([&](websocket_close_status close_status,
                            const utility::string_t& reason,
                            const std::error_code& error) {
    handleClose(close_status, reason, error);
  });
  // Connect
  return ws->connect(info.url).then([&]() {
    reconnect = false;
    connected = true;
    if(connectedHandler) {
      connectedHandler();
    }
    if(info.hasJwt) {
      nlohmann::json p = info.payload;
      p["token"] = convert_to_utf8(info.jwt);
      return this->send("token", p);
    }
    return pplx::task<void>();
  });
}

pplx::task<void> teckos::client::disconnect() noexcept
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  reconnect = false;
  if(ws) {
    return ws->close();
  }
  return {};
}

void teckos::client::handleClose(websocket_close_status status,
                                 const utility::string_t&,
                                 const std::error_code&)
{
  connected = false;
  if(status != websocket_close_status::normal) {
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
      reconnectionThread = std::make_unique<std::thread>(
          &teckos::client::reconnectionService, this);
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
          connect(info.url, info.jwt, info.payload).wait();
        } else {
          connect(info.url, info.jwt, {}).wait();
        }
      } else {
        connect(info.url).wait();
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

void teckos::client::handleMessage(const websocket_incoming_message& ret_msg)
{
  auto ret_str = ret_msg.extract_string().get();
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
        std::thread([=]() { msgHandler(data); }).detach();
      }
      // Inform event handler
      if(eventHandlers.count(event) > 0) {
        nlohmann::json payload;
        if(data.size() > 1)
          payload = data[1];
        eventHandlers[event](payload);
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

pplx::task<void> teckos::client::send(const std::string& event)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, {event, {}}, std::nullopt});
}

pplx::task<void> teckos::client::send(const std::string& event,
                                      const nlohmann::json& args)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, {event, args}, std::nullopt});
}

pplx::task<void> teckos::client::send(
    const std::string& event, const nlohmann::json& args,
    const std::function<void(const std::vector<nlohmann::json>&)>& callback)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  std::cout << "teckos::send" << std::endl;
  acks[fnId] = callback;
  return sendPackage({PacketType::EVENT, {event, args}, fnId++});
}

pplx::task<void> teckos::client::send(const nlohmann::json& args)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, args, std::nullopt});
}

pplx::task<void> teckos::client::sendPackage(teckos::packet p)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!connected) {
    throw std::runtime_error("Not connected");
  }
  websocket_outgoing_message msg;
  nlohmann::json jsonMsg = {{"type", p.type}, {"data", p.data}};
  if(p.number) {
    jsonMsg["id"] = *p.number;
  }
  msg.set_utf8_message(jsonMsg.dump());
  std::cout << "teckos::sendPackage" << std::endl;
  return ws->send(msg);
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
