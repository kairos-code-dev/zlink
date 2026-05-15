/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_SPOT_DISPATCH_CONTEXT_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_SPOT_DISPATCH_CONTEXT_INTERNAL_HPP_INCLUDED__

namespace zlink
{
class spot_dispatch_event_callback_context_t
{
  public:
    explicit spot_dispatch_event_callback_context_t (void *spot_);
    ~spot_dispatch_event_callback_context_t ();

    static void *current_handle ();

  private:
    void *_previous_handle;
};
}

#endif
