#include "teckos/rest.h"
#include <utility>                               // for pair, make_pair
#include "teckos/global.h"                       // for global

#include "spdlog/spdlog.h"

#ifdef USE_IX_WEBSOCKET
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXHttp.h>                  // for HttpRequestArgs
#include <memory>                                // for shared_ptr, make_shared
#include <stdexcept>                             // for runtime_error
#include <string>                                // for basic_string, allocator

teckos::RestResult teckos::rest::Get(const std::string &url, const Header header) {
  teckos::global::init();
  ix::HttpClient httpClient(false);
  auto args_ptr = std::make_shared<ix::HttpRequestArgs>();
  for (const auto &item: header) {
    args_ptr->extraHeaders.insert(std::make_pair(item.first, item.second));
  }
  auto response = httpClient.get(url, args_ptr);
  if (response->statusCode == 0) {
    throw std::runtime_error(std::to_string((int) response->errorCode) + ": " + response->errorMsg);
  }
  teckos::RestResult result;
  result.statusCode = response->statusCode;
  result.statusMessage = response->description;
  if (nlohmann::json::accept(response->body)) {
    result.body = nlohmann::json::parse(response->body);
  } else {
    result.body = nullptr;
  }
  return result;
}

teckos::RestResult teckos::rest::Post(const std::string &url, const Header header, const nlohmann::json &body) {
  teckos::global::init();
  ix::HttpClient httpClient(false);
  auto args_ptr = std::make_shared<ix::HttpRequestArgs>();
  for (const auto &item: header) {
    args_ptr->extraHeaders.insert(std::make_pair(item.first, item.second));
  }
  std::string strBody = "";
  if (!body.is_null()) {
    args_ptr->extraHeaders.insert({"content-type", "application/json"});
    strBody = body.dump();
  }
  auto response = httpClient.post(url, strBody, args_ptr);
  if (response->statusCode == 0) {
    throw std::runtime_error(std::to_string((int) response->errorCode) + ": " + response->errorMsg);
  }
  teckos::RestResult result;
  result.statusCode = response->statusCode;
  result.statusMessage = response->description;
  if (nlohmann::json::accept(response->body)) {
    result.body = nlohmann::json::parse(response->body);
  } else {
    result.body = nullptr;
  }
  return result;
}
#else
#include <cpprest/http_client.h>

teckos::RestResult teckos::rest::Get(const std::string &url, const Header header) {
  teckos::global::init();
  try {
      web::http::client::http_client client(utility::conversions::to_string_t(url));
      web::http::http_request request(web::http::methods::GET);
      for (const auto& item : header) {
          request.headers().add(utility::conversions::to_string_t(item.first),
              utility::conversions::to_string_t(item.second));
      }
      auto response = client.request(request).get();
      teckos::RestResult result;
      result.statusCode = response.status_code();
      result.statusMessage = utility::conversions::to_utf8string(response.reason_phrase());
      auto strBody = utility::conversions::to_utf8string(response.extract_string().get());
      if (nlohmann::json::accept(strBody)) {
          result.body = nlohmann::json::parse(strBody);
      }
      else {
          result.body = nullptr;
      }
      return result;
  }
  catch (std::exception &e) {
      spdlog::error("Caught exception during teckos::rest::Get: {}", e.what());
      return teckos::RestResult({ 500, e.what(), {} });
  }
}

teckos::RestResult teckos::rest::Post(const std::string &url, const Header header, const nlohmann::json &body) {
  teckos::global::init();
  try {
      web::http::client::http_client client(utility::conversions::to_string_t(url));
      web::http::http_request request(web::http::methods::POST);
      if (!body.is_null()) {
          request.set_body(body.dump(), "application/json");
      }
      for (const auto& item : header) {
          request.headers().add(utility::conversions::to_string_t(item.first), utility::conversions::to_string_t(item.second));
      }
      auto response = client.request(request).get();
      teckos::RestResult result;
      result.statusCode = response.status_code();
      result.statusMessage = utility::conversions::to_utf8string(response.reason_phrase());
      auto strBody = utility::conversions::to_utf8string(response.extract_string().get());
      if (nlohmann::json::accept(strBody)) {
          result.body = nlohmann::json::parse(strBody);
      }
      else {
          result.body = nullptr;
      }
      return result;
  }
  catch(std::exception& e) {
      spdlog::error("Caught exception during teckos::rest::Post: {}", e.what());
      return teckos::RestResult({500, e.what(), {}});
  }
}
#endif
