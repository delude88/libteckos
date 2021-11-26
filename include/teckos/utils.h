#pragma once

#ifdef _WIN32
#include <stringapiset.h>
#endif

namespace teckos {

namespace utils {
#ifdef _WIN32
typedef std::wstring string_t;
#else
typedef std::string string_t;
#endif

// https://stackoverflow.com/questions/215963/how-do-you-properly-use-widechartomultibyte
static std::string Convert_to_utf8(const string_t &potentiallywide) {
#ifdef _WIN32
  if(potentiallywide.empty())
  return std::string();
int size_needed =
    WideCharToMultiByte(CP_UTF8, 0, &potentiallywide[0],
                        (int)potentiallywide.size(), NULL, 0, NULL, NULL);
std::string strTo(size_needed, 0);
WideCharToMultiByte(CP_UTF8, 0, &potentiallywide[0],
                    (int)potentiallywide.size(), &strTo[0], size_needed, NULL,
                    NULL);
return strTo;
#else
  return potentiallywide;
#endif
}
}

}