/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include <memory>
#include <string>
#include <type_traits>

static_assert (std::is_same_v<decltype (zlink::framework::app_t::create ()),
                              zlink::framework::app_t>);

int
main ()
{
  auto app = zlink::framework::app_t::create ();

  app.config ()
    .load_json ("appsettings.json")
    .load_env ("ZLINK_");
  app.logging ().use_console ().set_level ("debug");

  bool zlink_configured = false;
  app.use_zlink ([&](zlink::framework::zlink_builder_t &) {
    zlink_configured = true;
  });

  const char *argv_raw[] = { "app", "--node=alpha", "--dry-run" };
  auto **argv = const_cast<char **> (argv_raw);
  const int exit_code = app.run (3, argv);

  if (exit_code != 0 || !zlink_configured) {
    return 1;
  }
  if (app.config ().model ().get ("config.json.path") != "appsettings.json") {
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
  if (!app.logging ().console_enabled () || app.logging ().level () != "debug") {
    return 6;
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
