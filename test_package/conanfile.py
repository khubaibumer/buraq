import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout


class BuraqTestPackageConan(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain", "VirtualRunEnv"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        opts = self.dependencies["buraq"].options
        cmake = CMake(self)
        cmake.configure(variables={
            "BURAQ_TEST_GLAZE": bool(opts.with_glaze),
            "BURAQ_TEST_NLOHMANN": bool(opts.with_nlohmann_json),
        })
        cmake.build()

    def test(self):
        if can_run(self):
            self.run(os.path.join(self.cpp.build.bindir, "test_package"), env="conanrun")
