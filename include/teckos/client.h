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

using namespace web::websockets::client;
using namespace pplx;

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
    bool hasJwt = false;
    std::string jwt;
    nlohmann::json payload = {};
  };

  // task_completion_event<void> tce;

  class client {
  public:
    client();
    ~client();

    void setReconnect(bool reconnect);

    bool shouldReconnect() const;

    void sendPayloadOnReconnect(bool sendPayloadOnReconnect);

    bool isSendingPayloadOnReconnect() const;

    void setTimeout(std::chrono::milliseconds ms);

    std::chrono::milliseconds getTimeout() const;

    void on(const std::string& event,
            const std::function<void(const nlohmann::json&)>& handler);

    void on_reconnected(const std::function<void()>& handler);
    void on_reconnecting(const std::function<void()>& handler);
    void on_connected(const std::function<void()>& handler);
    /**
     * Add an handler to get notified when the connection is closed.
     * Do only small tasks or create a separate thread when doing complex logic.
     * @param handler with parameter true, if normal exit, otherwise false
     */
    void on_disconnected(const std::function<void(bool)>& handler);

    void off(const std::string& event);

    void connect(const std::string& url) noexcept(false);

    void
    connect(const std::string& url, const std::string& jwt,
            const nlohmann::json& initialPayload) noexcept(false);

    bool isConnected() const;

    void disconnect();

    void setMessageHandler(
        const std::function<void(const std::vector<nlohmann::json>&)>& handler);

    pplx::task<void> emit(const std::string& event);

    pplx::task<void> emit(const std::string& event, const nlohmann::json& args);

    pplx::task<void>
    emit(const std::string& event, const nlohmann::json& args,
         const std::function<void(const std::vector<nlohmann::json>&)>&
             callback);

  protected:
    void service();

    void handleClose(websocket_close_status close_status,
                     const utility::string_t& reason,
                     const std::error_code& error);

    void handleMessage(const websocket_incoming_message& ret_msg);

    //void reconnect();

    pplx::task<void> send(std::initializer_list<nlohmann::json> args);

    pplx::task<void> sendPackage(packet p);

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
    std::unique_ptr<std::thread> connectionThread;
    bool shouldConnect;
    bool connected;
    //std::unique_ptr<std::thread> reconnectionThread;
    connection_settings settings;
    connection_info info;
    // pplx::task<void> receiveTask;
  };
} // namespace teckos

#endif // TECKOSCLIENT_H_
