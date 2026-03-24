/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CTX_BOOTSTRAP_HPP_INCLUDED__
#define __ZLINK_CTX_BOOTSTRAP_HPP_INCLUDED__

namespace zlink
{
class ctx_t;
class service_control_runtime_t;

class ctx_bootstrap_t
{
  public:
    static bool start_runtime_locked (ctx_t &ctx_);
    static service_control_runtime_t *ensure_service_runtime (ctx_t &ctx_);

  private:
    static void cleanup_failed_start_locked (ctx_t &ctx_);
};
}

#endif
