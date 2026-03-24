/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CTX_TERMINATION_HPP_INCLUDED__
#define __ZLINK_CTX_TERMINATION_HPP_INCLUDED__

namespace zlink
{
class ctx_t;

class ctx_termination_t
{
  public:
    static void teardown_runtime (ctx_t &ctx_);
    static void flush_pending_inproc_locked (ctx_t &ctx_);
    static bool begin_shutdown_locked (ctx_t &ctx_, bool allow_fork_cleanup_);
    static int wait_for_reaper_done (ctx_t &ctx_);
};
}

#endif
