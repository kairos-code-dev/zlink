/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/codecs/serializer.hpp>

namespace zlink::framework::extensions
{

template <typename... TPayloads> class protobuf_serializer_cache_t
{
  public:
    static serializer_registry_t &instance ()
    {
        static serializer_registry_t registry = [] {
            serializer_registry_t serializers;
            (serializers.template add_protobuf<TPayloads> (), ...);
            return serializers;
        } ();
        return registry;
    }
};

} // namespace zlink::framework::extensions
