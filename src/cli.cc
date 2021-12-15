#include <iostream>
#include <nlohmann/json.hpp>
#include <teckos/client.h>
#include <thread>

int main(int, char const *[]) {
  bool isReady = false;

  try {
    const std::string jwt = "mytoken";

    auto *client = new teckos::client();

    // Set message handler
    client->setMessageHandler([&](const std::vector<nlohmann::json> &msg) {
      // Do something here
      if (!msg.empty()) {
        std::cout << "Received event " << msg[0] << std::endl;
      }
    });

    // Handle an event
    client->on("ready", [&](const nlohmann::json &) {
      std::cout << "READY" << std::endl;
      isReady = true;
    });

    // Handle connection states
    client->on_connected([]() {
      std::cout << "Connected!" << std::endl;
    });
    client->on_disconnected([](bool normal) {
      std::cout << "Disconnected, " << (normal ? "as wished!" : "but didnt' want to!") << std::endl;
    });
    client->on_reconnected([]() {
      std::cout << "Reconnected, back there again!" << std::endl;
    });
    // Connect with jwt and empty payload, also wait till connection is
    // established
    std::cout << "Connecting..." << std::endl;
    client->setReconnect(true);
    client->sendPayloadOnReconnect(true);
    client->connect("ws://localhost:4000", jwt, {});
    std::cout << "Continue" << std::endl;

    int i = 0;
    while (i < 20) {
      std::cout << "THREAD ITERATION " << i << std::endl;
      if (isReady && client->isConnected()) {
        client->send("no-args");
        client->send("hello", {"First name", "Last name"});
        client->send("work", {{"some", "data"}}, [](auto &result) {
          std::cout << "Got result from callback: " << result << std::endl;
        });
        client->send("personal");
      }
      i++;
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    delete client;
  }
  catch (std::exception &err) {
    std::cerr << err.what() << std::endl;
  }
  return 0;
}
