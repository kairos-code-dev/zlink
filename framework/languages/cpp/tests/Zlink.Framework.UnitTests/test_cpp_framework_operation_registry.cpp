/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/foundation/operation_registry.hpp"

#include <cassert>
#include <stdexcept>

namespace foundation = zlink::framework::runtime::foundation;

foundation::operation_id_t id (std::uint8_t value)
{
    foundation::operation_id_t result{};
    result.back () = value;
    return result;
}

int main ()
{
    foundation::operation_registry_t registry (2);
    const auto now = foundation::operation_registry_t::clock_t::now ();
    int first_terminal = 0;
    int second_terminal = 0;
    assert (registry.register_operation (
      id (1), now + std::chrono::seconds (1),
      [&] (foundation::operation_terminal_t terminal, std::vector<std::uint8_t> payload) {
          assert (terminal == foundation::operation_terminal_t::completed);
          assert (payload == std::vector<std::uint8_t> ({7}));
          ++first_terminal;
      }));
    assert (registry.register_operation (
      id (2), now,
      [&] (foundation::operation_terminal_t terminal, std::vector<std::uint8_t>) {
          assert (terminal == foundation::operation_terminal_t::timed_out);
          ++second_terminal;
      }));
    assert (!registry.register_operation (id (3), now, [] (auto, auto) {}));
    assert (registry.complete (id (1), {7}));
    assert (!registry.complete (id (1), {8}));
    assert (registry.expire (now) == 1);
    assert (first_terminal == 1 && second_terminal == 1 && registry.size () == 0);
    assert (registry.shutdown () == 0);
    assert (!registry.register_operation (id (4), now, [] (auto, auto) {}));

    int failed_terminals = 0;
    foundation::operation_registry_t failed_registry (1);
    assert (failed_registry.register_operation (
      id (4), now + std::chrono::seconds (1),
      [&] (foundation::operation_terminal_t terminal,
           std::vector<std::uint8_t> payload) {
          assert (terminal
                  == foundation::operation_terminal_t::transport_failed);
          assert (payload.empty ());
          ++failed_terminals;
      }));
    assert (failed_registry.fail (
      id (4), foundation::operation_terminal_t::transport_failed));
    assert (!failed_registry.fail (
      id (4), foundation::operation_terminal_t::transport_failed));
    assert (failed_terminals == 1);

    int shutdown_terminals = 0;
    {
        foundation::operation_registry_t scoped_registry (2);
        assert (scoped_registry.register_operation (
          id (5), now,
          [&] (foundation::operation_terminal_t terminal, std::vector<std::uint8_t>) {
              assert (terminal == foundation::operation_terminal_t::shutdown);
              ++shutdown_terminals;
              throw std::runtime_error ("consumer failure");
          }));
        assert (scoped_registry.register_operation (
          id (6), now,
          [&] (foundation::operation_terminal_t terminal, std::vector<std::uint8_t>) {
              assert (terminal == foundation::operation_terminal_t::shutdown);
              ++shutdown_terminals;
          }));
    }
    assert (shutdown_terminals == 2);
    return 0;
}
