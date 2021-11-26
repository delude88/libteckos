#include "teckos/rest.h"

#ifdef USE_IX_WEBSOCKET
#include <ixwebsocket/IXHttpClient.h>

teckos::Result teckos::rest::Get(const std::string &url, const Header header) {
  ix::HttpClient httpClient(false);
  auto args_ptr = std::make_shared<ix::HttpRequestArgs>();
  for (const auto &item: header) {
    args_ptr->extraHeaders.insert(std::make_pair(item.first, item.second));
  }
  auto response = httpClient.get(url, args_ptr);
  if (response->statusCode == 0) {
    throw std::runtime_error(std::to_string((int) response->errorCode) + ": " + response->errorMsg);
  }
  teckos::Result result;
  result.statusCode = response->statusCode;
  result.statusMessage = response->description;
  if (nlohmann::json::accept(response->body)) {
    result.body = nlohmann::json::parse(response->body);
  } else {
    result.body = nullptr;
  }
  return result;
}

teckos::Result teckos::rest::Post(const std::string &url, const Header header, const nlohmann::json &body) {
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
  teckos::Result result;
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
#include "teckos/utils.h"
#include <cpprest/http_client.h>

teckos::Result teckos::rest::Get(const std::string &url, const Header header) {
  web::http::client::http_client client(U(url));
  web::http::http_request request(web::http::methods::GET);
  for (const auto &item: header) {
    request.headers().add(U(item.first), U(item.second));
  }
  auto response = client.request(request).get();
  teckos::Result result;
  result.statusCode = response.status_code();
  result.statusMessage = teckos::utils::Convert_to_utf8(response.reason_phrase());
  auto strBody = teckos::utils::Convert_to_utf8(response.extract_string().get());
  if (nlohmann::json::accept(strBody)) {
    result.body = nlohmann::json::parse(strBody);
  } else {
    result.body = nullptr;
  }
  return result;
}

teckos::Result teckos::rest::Post(const std::string &url, const Header header, const nlohmann::json &body) {
  web::http::client::http_client client(U(url));
  web::http::http_request request(web::http::methods::POST);
  if (!body.is_null()) {
    request.set_body(body.dump(), "application/json");
  }
  for (const auto &item: header) {
    request.headers().add(U(item.first), U(item.second));
  }
  auto response = client.request(request).get();
  teckos::Result result;
  result.statusCode = response.status_code();
  result.statusMessage = teckos::utils::Convert_to_utf8(response.reason_phrase());
  auto strBody = teckos::utils::Convert_to_utf8(response.extract_string().get());
  if (nlohmann::json::accept(strBody)) {
    result.body = nlohmann::json::parse(strBody);
  } else {
    result.body = nullptr;
  }
  return result;
}
#endif