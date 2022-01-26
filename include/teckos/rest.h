//
// Created by Tobias Hegemann on 26.11.21.
//
#pragma once

#include <iosfwd>                 // for string
#include <map>                    // for map, map<>::value_compare
#include <nlohmann/json.hpp>      // for basic_json
#include <nlohmann/json_fwd.hpp>  // for json

namespace teckos {

	namespace REST {
		struct Result {
			int statusCode;
			std::string statusMessage;
			nlohmann::json body;
		};

	}

typedef std::map<std::string, std::string> Header;
class rest {
 public:
  static REST::Result Get(const std::string &url, const Header header = Header());

  static REST::Result Post(const std::string &url, const Header header = Header(), const nlohmann::json &body = nullptr);
};
}