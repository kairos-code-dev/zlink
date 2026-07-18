{
  "targets": [
    {
      "target_name": "zlink",
      "sources": [
        "native/src/addon.cc",
        "native/src/addon_core.cc",
        "native/src/addon_core_options.cc",
        "native/src/addon_exports.cc",
        "native/src/addon_mesh_service.cc",
        "native/src/addon_spot_request_callbacks.cc"
      ],
      "include_dirs": [ "../../core/include" ],
      "variables": {
        "zlink_root%": "<!(node -p \"process.cwd()\")",
        "zlink_lib%": "<!(node -p \"process.env.ZLINK_LIB_PATH || ''\")"
      },
      "conditions": [
        [ "\"<(zlink_lib)\" != \"\"",
          { "libraries": [ "<(zlink_lib)" ] },
          {
            "conditions": [
              [ "OS==\"linux\"", { "libraries": [ "<(zlink_root)/../../core/build/lib/libzlink.so" ] } ],
              [ "OS==\"mac\"", { "libraries": [ "<(zlink_root)/../../core/build/lib/libzlink.dylib" ] } ],
              [ "OS==\"win\"", { "libraries": [ "<(zlink_root)/../../core/build/lib/zlink.lib" ] } ]
            ]
          }
        ],
        [ "OS==\"linux\"", { "ldflags": [ "-Wl,-rpath,\\$$ORIGIN" ] } ],
        [ "OS==\"mac\"", {
          "ldflags": [ "-Wl,-rpath,@loader_path" ],
          "xcode_settings": {
            "LD_RUNPATH_SEARCH_PATHS": [ "@loader_path" ]
          }
        } ]
      ]
    }
  ]
}
