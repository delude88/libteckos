//
// Created by Tobias Hegemann on 26.11.21.
//
#pragma once

#include <string>
#include <map>
#include <nlohmann/json.hpp>

namespace teckos {
struct Result {
  int statusCode;
  std::string statusMessage;
  nlohmann::json body;
};
typedef std::map<std::string, std::string> Header;
class rest {
 public:
  static Result Get(const std::string &url, const Header header = Header());

  static Result Post(const std::string &url, const Header header = Header(), const nlohmann::json &body = nullptr);
};
}