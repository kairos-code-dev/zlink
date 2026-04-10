/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DISPATCH_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DISPATCH_INTERNAL_HPP_INCLUDED__

namespace zlink
{
class spot_dispatch_context_t
{
  public:
    spot_dispatch_context_t (void *handle_, bool is_node_);
    ~spot_dispatch_context_t ();

    static void *current_handle ();
    static bool current_is_node ();

  private:
    void *_previous_handle;
    bool _previous_is_node;
};

void *current_spot_dispatch_handle ();
bool current_spot_dispatch_is_node ();
}

#endif
