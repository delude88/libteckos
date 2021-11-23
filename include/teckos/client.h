//
// Created by Tobias Hegemann on 30.04.21.
//

#ifndef TECKOSCLIENT_H_
#define TECKOSCLIENT_H_

#include <chrono>
#include <future>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/client.hpp>

typedef websocketpp::server<websocketpp::config::asio_tls> server;
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;

namespace teckos {

#ifdef _WIN32
  typedef std::wstring string_t;
#else
  typedef std::string string_t;
#endif

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

    [[nodiscard]] bool shouldReconnect() const noexcept;

    void sendPayloadOnReconnect(bool sendPayloadOnReconnect) noexcept;

    [[maybe_unused]] [[nodiscard]] bool
    isSendingPayloadOnReconnect() const noexcept;

    void setTimeout(std::chrono::milliseconds ms) noexcept;

    [[maybe_unused]] [[nodiscard]] std::chrono::milliseconds
    getTimeout() const noexcept;

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

    std::future<void> connect(const string_t& url) noexcept(false);

    std::future<void>
    connect(const string_t& url, const string_t& jwt,
            const nlohmann::json& initialPayload) noexcept(false);

    [[nodiscard]] bool isConnected() const noexcept;

    std::future<void> disconnect() noexcept;

    void setMessageHandler(
        const std::function<void(const std::vector<nlohmann::json>&)>&
            handler) noexcept;

    std::future<void> send(const std::string& event) noexcept(false);

    std::future<void> send(const std::string& event,
                           const nlohmann::json& args) noexcept(false);

    std::future<void>
    send(const std::string& event, const nlohmann::json& args,
         const std::function<void(const std::vector<nlohmann::json>&)>&
             callback) noexcept(false);

  protected:
    std::future<void> connect();

    void reconnectionService();

    void handleClose(websocketpp::websocket_close_status close_status,
                     const teckos::string_t& reason,
                     const std::error_code& error);

    void handleMessage(client* c, websocketpp::connection_hdl hdl, websocketpp::message_ptr msg);

    std::future<void> send(const nlohmann::json& args) noexcept(false);

    std::future<void> sendPackage(packet p) noexcept(false);

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
    std::shared_ptr<websocketpp::client> ws;
    std::unique_ptr<std::thread> reconnectionThread;
    bool reconnect;
    bool connected;
    connection_settings settings;
    connection_info info;
  };
} // namespace teckos

#endif // TECKOSCLIENT_H_
