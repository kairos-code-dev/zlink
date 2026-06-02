/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <type_traits>

static_assert (std::is_same_v<decltype (zlink::framework::app_t::create ()),
                              zlink::framework::app_t>);

int
main ()
{
  auto app = zlink::framework::app_t::create ();
  const auto config_path =
    std::filesystem::temp_directory_path () / "zlink_cpp_framework_app.json";
  {
    std::ofstream config (config_path);
    config << R"({"node":"json-node","limits":{"queue":42},"enabled":true})";
  }
  setenv ("ZLINK_REGION", "local", 1);

  app.config ()
    .load_json (config_path.string ())
    .load_env ("ZLINK_");
  const auto log_path =
    std::filesystem::temp_directory_path () / "zlink_cpp_framework_app.log";
  std::vector<zlink::framework::log_record_t> observed_logs;
  app.logging ()
    .use_console ()
    .use_file (log_path.string ())
    .use_callback_sink ([&](const zlink::framework::log_record_t &record) {
      observed_logs.push_back (record);
    })
    .use_async ({ .queue_capacity = 128 })
    .use_backend (zlink::framework::logging_backend_t::spdlog)
    .set_min_level (zlink::framework::log_level_t::debug);
  auto logger = app.logging ().factory ().create ("app-host-test");
  logger.debug ("startup", { { "node", "alpha" } });
  logger.trace ("filtered");

  bool zlink_configured = false;
  app.use_zlink ([&](zlink::framework::zlink_builder_t &) {
    zlink_configured = true;
  });

  const char *argv_raw[] = { "app", "--node=alpha", "--dry-run" };
  auto **argv = const_cast<char **> (argv_raw);
  std::thread stopper ([&app] {
    std::this_thread::sleep_for (std::chrono::milliseconds (5));
    app.stop ();
  });
  const int exit_code = app.run (3, argv);
  stopper.join ();

  if (exit_code != 0 || !zlink_configured) {
    return 1;
  }
  if (app.config ().model ().get ("config.json.path") !=
      config_path.string ()) {
    return 2;
  }
  if (app.config ().model ().get ("config.env.prefix") != "ZLINK_") {
    return 3;
  }
  if (app.config ().model ().get ("cli.node") != "alpha") {
    return 4;
  }
  if (app.config ().model ().get ("cli.dry-run") != "true") {
    return 5;
  }
  if (app.config ().model ().get ("node") != "json-node" ||
      app.config ().model ().get ("limits.queue") != "42" ||
      app.config ().model ().get ("enabled") != "true" ||
      app.config ().model ().get ("env.REGION") != "local" ||
      app.config ().model ().get ("host.signal_handlers") != "installed") {
    return 10;
  }
  if (!app.logging ().console_enabled () || app.logging ().level () != "debug") {
    return 6;
  }
  if (!app.logging ().async_enabled () ||
      app.logging ().backend () != zlink::framework::logging_backend_t::spdlog ||
      app.logging ().file_paths ().empty () ||
      observed_logs.size () != 1 ||
      observed_logs.front ().category != "app-host-test" ||
      observed_logs.front ().fields.front ().key != "node" ||
      app.logging ().captured_records ().size () != 1) {
    return 7;
  }
  {
    std::ifstream log_file (log_path);
    std::string line;
    std::getline (log_file, line);
    if (line.find ("startup") == std::string::npos ||
        line.find ("node=alpha") == std::string::npos) {
      return 11;
    }
  }

  struct singleton_service_t {
    int value = 7;
  };
  app.services ().add_singleton<singleton_service_t> ();
  auto provider = app.services ().build_provider ();
  if (provider.get_required<singleton_service_t> ().value != 7) {
    return 8;
  }

  app.stop ();
  if (app.run (1, argv) != 0) {
    return 9;
  }

  return 0;
}
