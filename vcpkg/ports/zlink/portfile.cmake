vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ulala-x/zlink
    REF 0.6
    SHA512 26c4c3e463fbcd5a7809bb4f667930ced1eabb05d749db79dd61056c6da8e70ffb67767682a030dc1a6260814c1fcabf72c1fa695c822d46cc1db78004846eb5
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_SHARED=ON
        -DBUILD_STATIC=OFF
        -DWITH_TLS=ON
        -DZLINK_CXX_STANDARD=17
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/zlink)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
