import os
import re

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.files import copy, load
from conan.tools.layout import basic_layout

required_conan_version = ">=2.0"


class BuraqConan(ConanFile):
    name = "buraq"
    description = "Header-only, zero-dependency C++20/23 SDK for the Agent Client Protocol (ACP) v1"
    license = "Apache-2.0"
    url = "https://github.com/khubaibumer/buraq"
    homepage = "https://github.com/khubaibumer/buraq"
    topics = ("agent-client-protocol", "acp", "json-rpc", "header-only", "ai-agents")
    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "with_glaze": [True, False],
        "with_nlohmann_json": [True, False],
    }
    default_options = {
        "with_glaze": False,
        "with_nlohmann_json": False,
    }
    exports_sources = "include/*", "LICENSE"
    no_copy_source = True

    def set_version(self):
        if not self.version:
            cmake = load(self, os.path.join(self.recipe_folder, "CMakeLists.txt"))
            self.version = re.search(r"project\(buraq VERSION ([0-9]+\.[0-9]+\.[0-9]+)", cmake).group(1)

    def layout(self):
        basic_layout(self, src_folder=".")

    def requirements(self):
        # The codecs are optional headers; the options only pull in the JSON library they include.
        if self.options.with_glaze:
            self.requires("glaze/[>=4.4.3 <10]", transitive_headers=True)
        if self.options.with_nlohmann_json:
            self.requires("nlohmann_json/[>=3.11.3 <4]", transitive_headers=True)

    def package_id(self):
        self.info.clear()

    def validate(self):
        check_min_cppstd(self, 20)

    def package(self):
        copy(self, "LICENSE", self.source_folder, os.path.join(self.package_folder, "licenses"))
        copy(self, "*.hpp", os.path.join(self.source_folder, "include"), os.path.join(self.package_folder, "include"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "buraq")
        self.cpp_info.set_property("cmake_target_name", "buraq::buraq")
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.system_libs = ["pthread"]
