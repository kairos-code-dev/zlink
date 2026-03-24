/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

#include "services/spot/spot_dispatch_internal.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

namespace zlink
{
service_public_api_guard_t *spot_public_api_guard_for_testing (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (spot)
        return &spot->public_api;
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_))
        return pub->node () ? &pub->node ()->public_api_guard () : NULL;
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->node () ? &sub->node ()->public_api_guard () : NULL;
    return NULL;
}

void destroy_spot_handle_for_testing (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot) {
        if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_)) {
            if (pub->node ())
                pub->node ()->public_api_guard ().cancel_close ();
            int rc = pub->destroy ();
            if (rc != 0)
                rc = pub->destroy_from_node ();
            zlink_assert (rc == 0);
            delete pub;
            return;
        }
        if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_)) {
            if (sub->node ())
                sub->node ()->public_api_guard ().cancel_close ();
            int rc = sub->destroy ();
            if (rc != 0)
                rc = sub->destroy_from_node ();
            zlink_assert (rc == 0);
            delete sub;
            return;
        }
        return;
    }

    if (spot->sub) {
        int rc = spot->sub->destroy ();
        if (rc != 0)
            rc = spot->sub->destroy_from_node ();
        zlink_assert (rc == 0);
        delete spot->sub;
        spot->sub = NULL;
    }
    if (spot->pub) {
        int rc = spot->pub->destroy ();
        if (rc != 0)
            rc = spot->pub->destroy_from_node ();
        zlink_assert (rc == 0);
        delete spot->pub;
        spot->pub = NULL;
    }

    spot->tag = 0xdeadbeef;
    delete spot;
}

}
