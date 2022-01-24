//
// Created by Tobias Hegemann on 26.11.21.
//
#pragma once

#include <iosfwd>                 // for string
#include <map>                    // for map, map<>::value_compare
#include <nlohmann/json.hpp>      // for basic_json
#include <nlohmann/json_fwd.hpp>  // for json

namespace teckos {
struct RestResult {
  int statusCode;
  std::string statusMessage;
  nlohmann::json body;
};
typedef std::map<std::string, std::string> Header;
class rest {
 public:
  static RestResult Get(const std::string &url, const Header header = Header());

  static RestResult Post(const std::string &url, const Header header = Header(), const nlohmann::json &body = nullptr);
};
}