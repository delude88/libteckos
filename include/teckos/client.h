//
// Created by Tobias Hegemann on 30.04.21.
//

#ifndef TECKOSCLIENT_H_
#define TECKOSCLIENT_H_

#include <memory>                     // for shared_ptr
#include <chrono>                     // for milliseconds
#include <cstdint>                    // for uint32_t
#include <iostream>                   // for string
#include <map>                        // for map
#include <mutex>                      // for recursive_mutex
#include <nlohmann/json.hpp>          // for json
#include <optional>                   // for optional
#include <string>                     // for basic_string
#include <thread>                     // for thread
#include <vector>                     // for vector

#ifdef USE_IX_WEBSOCKET
#include <ixwebsocket/IXWebSocket.h>             // for WebSocket
#include "ixwebsocket/IXNetSystem.h"             // for initNetSystem
#include "ixwebsocket/IXWebSocketCloseInfo.h"    // for WebSocketCloseInfo
#include "ixwebsocket/IXWebSocketErrorInfo.h"    // for WebSocketErrorInfo
#include "ixwebsocket/IXWebSocketMessage.h"      // for WebSocketMessagePtr
#include "ixwebsocket/IXWebSocketMessageType.h"  // for WebSocketMessageType
using WebSocketClient = ix::WebSocket;
#else
#include <cpprest/ws_client.h>
using WebSocketClient = web::websockets::client::websocket_callback_client;
#endif

namespace teckos {

enum PacketType { EVENT = 0, ACK = 1 };
struct packet {
  PacketType type;
  nlohmann::json data;
  std::optional<uint32_t> number;
};
struct connection_settings {
  bool reconnect = false;
  bool sendPayloadOnReconnect = false;
};
struct connection_info {
  std::string url;
  bool hasJwt;
  std::string jwt;
  nlohmann::json payload;
};

using Result = const std::vector<nlohmann::json> &;
using Callback = std::function<void(Result)>;

class client {
 public:
  explicit client(bool async_events = false) noexcept;
  ~client();

  void setReconnect(bool reconnect) noexcept;

  [[nodiscard]] bool shouldReconnect() const noexcept;

  void sendPayloadOnReconnect(bool sendPayloadOnReconnect) noexcept;

  [[nodiscard]] bool isSendingPayloadOnReconnect() const noexcept;

  void setTimeout(std::chrono::milliseconds ms) noexcept;

  std::chrono::milliseconds getTimeout() const noexcept;

  void on(const std::string &event,
          const std::function<void(const nlohmann::json &)> &handler);

  void on_reconnected(const std::function<void()> &handler) noexcept;
  void on_connected(const std::function<void()> &handler) noexcept;
  /**
   * Add an handler to get notified when the connection is closed.
   * Do only small tasks or create a separate thread when doing complex logic.
   * @param handler with parameter true, if normal exit, otherwise false
   */
  void on_disconnected(const std::function<void(bool)> &handler) noexcept;

  void off(const std::string &event);

  /**
   * Connect to the given url.
   * When this client is already connected, it will disconnect first.
   * This method may forward exceptions from underlying services.
   * @param url
   */
  void connect(const std::string &url) noexcept(false);

  /**
   * Connect to the given url using an JSON Web Token and initial payload.
   * When this client is already connected, it will disconnect first.
   * This method may forward exceptions from underlying services.
   * @param url
   * @param jwt
   * @param initialPayload
   */
  void connect(const std::string &url, const std::string &jwt,
               const nlohmann::json &initialPayload) noexcept(false);

  [[nodiscard]] bool isConnected() const noexcept;

  /**
   * This will disconnect from the server.
   * If the client is not connected, the method will just return.
   * This method may forwards exceptions from underlying services.
   */
  void disconnect();

  void setMessageHandler(
      const std::function<void(const std::vector<nlohmann::json> &)> &
      handler) noexcept;

  void send(const std::string &event) noexcept(false);

  void send(const std::string &event,
            const nlohmann::json &args) noexcept(false);

  void send(const std::string &event, const nlohmann::json &args,
            Callback callback) noexcept(false);

 protected:
  void connect();

  void reconnect();

  void handleClose(int code, const std::string &reason);

  void handleMessage(const std::string &msg) noexcept;

  [[maybe_unused]] void send_json(const nlohmann::json &args) noexcept(false);

  void sendPackage(packet p) noexcept(false);

 private:
  std::chrono::milliseconds timeout = std::chrono::milliseconds(500);

  std::function<void()> connectedHandler;
  std::function<void()> reconnectedHandler;
  std::function<void(bool)> disconnectedHandler;
  std::function<void(const std::vector<nlohmann::json> &)> msgHandler;
  std::map<std::string, std::function<void(const nlohmann::json &)>>
      eventHandlers;
  std::recursive_mutex mutex;
  uint32_t fnId = 0;
  std::map<uint32_t, Callback>
      acks;
  std::unique_ptr<WebSocketClient> ws;
  bool reconnecting;
  bool connected;
  bool authenticated;
  bool async_events;
  connection_settings settings;
  connection_info info;
  std::vector<std::thread> threadPool;

#ifndef USE_IX_WEBSOCKET
  std::thread reconnectThread;
#endif
};
} // namespace teckos

#endif // TECKOSCLIENT_H_
