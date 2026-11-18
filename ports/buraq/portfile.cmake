set(VCPKG_BUILD_TYPE release) # header-only

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO khubaibumer/buraq
    REF "v${VERSION}"
    SHA512 dd77bb799a33149d95cc5c0287727dc9d88fd6d5bdfc785603410d309f268c1c947d3e854bca15df4e2f7dd1860dd6c00f01482be24a3e67920eda77b22823b2
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBURAQ_BUILD_EXAMPLES=OFF
        -DBURAQ_BUILD_TESTS=OFF
        -DBURAQ_FETCH_CODECS=OFF
        -DBURAQ_INSTALL=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/buraq)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
