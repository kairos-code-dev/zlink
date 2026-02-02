{
  "targets": [
    {
      "target_name": "zlink",
      "sources": [
        "native/src/addon.cc",
        "native/src/addon_core.cc",
        "native/src/addon_discovery.cc",
        "native/src/addon_spot.cc"
      ],
      "include_dirs": [ "../../core/include" ],
      "variables": {
        "zlink_root%": "<!(pwd)"
      },
      "libraries": [ "<(zlink_root)/../../build_cpp/lib/libzlink.so" ]
    }
  ]
}
