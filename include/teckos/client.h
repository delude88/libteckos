//
// Created by Tobias Hegemann on 30.04.21.
//

#ifndef TECKOSCLIENT_H_
#define TECKOSCLIENT_H_

#include <chrono>
#include <cpprest/ws_client.h>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <optional>

using namespace web::websockets::client;
using namespace pplx;

namespace teckos {

  typedef utility::string_t string_t;

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
    string_t url;
    bool hasJwt = false;
    string_t jwt = {};
    nlohmann::json payload = {};
  };

  class client {
  public:
    client() noexcept;
    ~client();

    void setReconnect(bool reconnect) noexcept;

    bool shouldReconnect() const noexcept;

    void sendPayloadOnReconnect(bool sendPayloadOnReconnect) noexcept;

    bool isSendingPayloadOnReconnect() const noexcept;

    void setTimeout(std::chrono::milliseconds ms) noexcept;

    std::chrono::milliseconds getTimeout() const noexcept;

    void on(const std::string& event,
            const std::function<void(const nlohmann::json&)>& handler);

    void on_reconnected(const std::function<void()>& handler) noexcept;
    void on_reconnecting(const std::function<void()>& handler) noexcept;
    void on_connected(const std::function<void()>& handler) noexcept;
    /**
     * Add an handler to get notified when the connection is closed.
     * Do only small tasks or create a separate thread when doing complex logic.
     * @param handler with parameter true, if normal exit, otherwise false
     */
    void on_disconnected(const std::function<void(bool)>& handler) noexcept;

    void off(const std::string& event);

    pplx::task<void> connect(const string_t& url) noexcept(false);

    pplx::task<void>
    connect(const string_t& url, const string_t& jwt,
            const nlohmann::json& initialPayload) noexcept(false);

    bool isConnected() const noexcept;

    pplx::task<void> disconnect() noexcept;

    void setMessageHandler(
        const std::function<void(const std::vector<nlohmann::json>&)>&
            handler) noexcept;

    pplx::task<void> emit(const std::string& event) noexcept(false);

    pplx::task<void> emit(const std::string& event,
                          const nlohmann::json& args) noexcept(false);

    pplx::task<void>
    emit(const std::string& event, const nlohmann::json& args,
         const std::function<void(const std::vector<nlohmann::json>&)>&
             callback) noexcept(false);

  protected:
    pplx::task<void> connect();

    void reconnectionService();

    void handleClose(websocket_close_status close_status,
                     const utility::string_t& reason,
                     const std::error_code& error);

    void handleMessage(const websocket_incoming_message& ret_msg);

    pplx::task<void> send(const nlohmann::json& args) noexcept(false);

    pplx::task<void> sendPackage(packet p) noexcept(false);

  private:
    std::chrono::milliseconds timeout = std::chrono::milliseconds(500);
    std::function<void()> connectedHandler;
    std::function<void()> reconnectingHandler;
    std::function<void()> reconnectedHandler;
    std::function<void(bool)> disconnectedHandler;
    std::function<void(const std::vector<nlohmann::json>&)> msgHandler;
    std::map<std::string, std::function<void(const nlohmann::json&)>>
        eventHandlers;
    std::recursive_mutex mutex;
    uint32_t fnId = 0;
    std::map<uint32_t, std::function<void(const std::vector<nlohmann::json>&)>>
        acks;
    std::shared_ptr<websocket_callback_client> ws;
    std::unique_ptr<std::thread> reconnectionThread;
    bool reconnect;
    bool connected;
    connection_settings settings;
    connection_info info;
  };
} // namespace teckos

#endif // TECKOSCLIENT_H_
