{
  "targets": [
    {
      "target_name": "zlink",
      "sources": [ "src/addon.cc" ],
      "include_dirs": [ "../../core/include" ],
      "variables": {
        "zlink_lib_dir%": "../../build"
      },
      "libraries": [ "-L<(zlink_lib_dir)", "-lzlink" ]
    }
  ]
}
