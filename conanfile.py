from conans import ConanFile, CMake


class TeckosConan(ConanFile):
    settings = ["os", "compiler", "build_type", "arch"]
    requires = ["nlohmann_json/3.10.5", "cpprestsdk/2.10.18"]
    generators = "cmake", "cmake_find_package", "json"

    def configure(self):
        if self.settings.os == "Linux":
            self.requires.add("openssl/1.1.1l")
