#include "teckos/client.h"

teckos::client::client()
    : connected(false), shouldConnect(false)
{
  /*
  ws->set_message_handler([&](const websocket_incoming_message& ret_msg) {
    handleMessage(ret_msg);
  });
  ws->set_close_handler([&](websocket_close_status close_status,
                           const utility::string_t& reason,
                           const std::error_code& error) {
    std::cout << "HANDLE CLOSE" << std::endl;
    handleClose(close_status, reason, error);
  });
   */
}

teckos::client::~client()
{
  shouldConnect = false;
  if( connectionThread ) {
    connectionThread->join();
  }
  /*if(reconnectionThread) {
    reconnectionThread->join();
  }
  if(connected) {
    disconnect();
  }*/
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
  info = {url, false};
  shouldConnect = true;
  if( !connectionThread ) {
    connectionThread.reset(new std::thread(&teckos::client::service, this));
  }
  /*
  if(connected) {
    disconnect().wait();
  }
  shouldConnect = true;
  return ws->connect(U(url)).then([&]() {
    connected = true;
    if(connectedHandler) {
      connectedHandler();
    }
  });*/
}

void
teckos::client::connect(const std::string& url, const std::string& jwt,
                        const nlohmann::json& initialPayload) noexcept(false)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  info = {url, true, jwt, initialPayload};
  shouldConnect = true;
  if( !connectionThread ) {
    connectionThread.reset(new std::thread(&teckos::client::service, this));
  }
  /*
  if(connected) {
    disconnect().wait();
  }
  shouldConnect = true;
  std::cout << "Try to reconnect..." << std::endl;
  return ws->connect(U(url)).then([&]() {
    std::cout << "OK!" << std::endl;
    connected = true;
    if(connectedHandler) {
      std::cout << "CALLING connectedHandler" << std::endl;
      connectedHandler();
    }
    nlohmann::json p = initialPayload;
    p["token"] = jwt;
    std::cout << "Sending token" << std::endl;
    return this->emit("token", p);
  });*/
}

void teckos::client::service()
{
  while(shouldConnect) {
    if(!connected) {
      std::unique_lock<std::recursive_mutex> lock(mutex);
      try {
        ws.reset(new websocket_callback_client());
        ws->set_message_handler([&](const websocket_incoming_message& ret_msg) {
          handleMessage(ret_msg);
        });
        ws->set_close_handler([&](websocket_close_status close_status,
                                  const utility::string_t& reason,
                                  const std::error_code& error) {
          std::cout << "HANDLE CLOSE" << std::endl;
          handleClose(close_status, reason, error);
        });
        std::cout << "Connecting to " << info.url << std::endl;
        std::cout << "HOST:" << ws->uri().host() << std::endl;
        std::cout << "WILL Connecting to " << info.url << std::endl;
        auto bla = ws->connect(U(info.url));
        std::cout << "NOW Connecting to " << info.url << std::endl;
        bla.wait();
        std::cout << "Sucessfully connected!" << std::endl;
        connected = true;
        if(connectedHandler) {
          connectedHandler();
        }
        if(info.hasJwt) {
          nlohmann::json p = info.payload;
          p["token"] = info.jwt;
          std::cout << "Sending token" << std::endl;
          this->emit("token", p);
        }
      }
      catch(...) {
        std::cout << "Connecting failed!" << std::endl;
      }
      lock.unlock();
      std::this_thread::sleep_for(timeout);
    }
  }
  // Disconnect
  if( ws ) {
    ws->close().then([&]() { connected = false; });
  }
}

void teckos::client::disconnect()
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  shouldConnect = false;
  if( connectionThread ) {
    connectionThread->join();
  }
  /*
  // tce.set();
  return ws->close().then([&]() { connected = false; });*/
}


void teckos::client::handleClose(websocket_close_status status,
                                 const utility::string_t&,
                                 const std::error_code&)
{
  std::cout << "handleClose" << std::endl;
  connected = false;
}
/*
void teckos::client::handleClose(websocket_close_status status,
                                 const utility::string_t&,
                                 const std::error_code&)
{
  connected = false;
  if(disconnectedHandler) {
    disconnectedHandler(status == websocket_close_status::normal);
  }
  if(!reconnecting && settings.reconnect) {
    if(reconnectionThread) {
      reconnectionThread->join();
    }
    reconnecting = true;
    reconnectionThread.reset(new std::thread(&teckos::client::reconnect, this));
  }
}
void teckos::client::reconnect()
{
  std::this_thread::sleep_for(timeout);
  while(shouldConnect && !connected && settings.reconnect) {
    if(reconnectingHandler) {
      reconnectingHandler();
    }
    try {
      if(info.hasJwt) {
        if(settings.sendPayloadOnReconnect) {
          std::cout << "YUP" << std::endl;
          connect(info.url, info.jwt, info.payload).wait();
          std::cout << "DONE" << std::endl;
        } else {
          connect(info.url, info.jwt, {}).wait();
        }
      } else {
        connect(info.url).wait();
      }
      if(reconnectedHandler) {
        std::cout << "Calling" << std::endl;
        reconnectedHandler();
      }
    }
    catch(...) {
      std::cout << "EXCEPTION" << std::endl;
      std::this_thread::sleep_for(timeout);
    }
  }
  std::cout << "Finished thread" << std::endl;
  reconnecting = false;
}*/

void teckos::client::handleMessage(const websocket_incoming_message& ret_msg)
{
  auto ret_str = ret_msg.extract_string().get();
  if(ret_str == "hey")
    return;
  nlohmann::json j = nlohmann::json::parse(ret_str);
  const PacketType type = j["type"];
  if(type == PacketType::EVENT) {
    const nlohmann::json jsonData = j["data"];
    if(jsonData.is_array() && !jsonData.empty()) {
      std::vector<nlohmann::json> data = j["data"];
      // Inform message handler
      if(msgHandler) {
        msgHandler(data);
      }
      // Inform event handler
      const std::string event = data[0];
      if(eventHandlers.count(event) > 0) {
        nlohmann::json payload;
        if(data.size() > 1)
          payload = data[1];
        eventHandlers[event](payload);
      }
    }
  } else {
    // type === PacketType::ACK
    // We have to call the function
    const int32_t id = j["id"];
    if(acks.count(id) > 0) {
      acks.at(id)(j["data"]);
    }
  }
}

void teckos::client::setMessageHandler(
    const std::function<void(const std::vector<nlohmann::json>&)>& handler)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  msgHandler = handler;
}

pplx::task<void> teckos::client::emit(const std::string& event)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, {event, {}}, std::nullopt});
}

pplx::task<void> teckos::client::emit(const std::string& event,
                                      const nlohmann::json& args)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sendPackage({PacketType::EVENT, {event, args}, std::nullopt});
}

pplx::task<void> teckos::client::emit(
    const std::string& event, const nlohmann::json& args,
    const std::function<void(const std::vector<nlohmann::json>&)>& callback)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  acks[fnId] = callback;
  return sendPackage({PacketType::EVENT, {event, args}, fnId++});
}

pplx::task<void>
teckos::client::send(std::initializer_list<nlohmann::json> args)
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
  return ws->send(msg);
}

void teckos::client::setTimeout(std::chrono::milliseconds ms)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  timeout = ms;
}

std::chrono::milliseconds teckos::client::getTimeout() const
{
  return timeout;
}

bool teckos::client::isConnected() const
{
  return connected;
}

void teckos::client::setReconnect(bool reconnect)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  settings.reconnect = reconnect;
}

bool teckos::client::shouldReconnect() const
{
  return settings.reconnect;
}

void teckos::client::sendPayloadOnReconnect(bool sendPayloadOnReconnect)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  settings.sendPayloadOnReconnect = sendPayloadOnReconnect;
}

bool teckos::client::isSendingPayloadOnReconnect() const
{
  return settings.sendPayloadOnReconnect;
}

void teckos::client::on_connected(const std::function<void()>& handler)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  connectedHandler = handler;
}

void teckos::client::on_reconnected(const std::function<void()>& handler)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  reconnectedHandler = handler;
}

void teckos::client::on_reconnecting(const std::function<void()>& handler)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  reconnectingHandler = handler;
}

void teckos::client::on_disconnected(const std::function<void(bool)>& handler)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  disconnectedHandler = handler;
}
