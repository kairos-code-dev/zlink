/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

/* 공통 sample spec §7: Application은 인프라 구현이 아니라 port에 의존한다.
 * room route 레코드의 저장소 구현(Redis)은 Configuration/Infrastructure가 소유한다. */

#include "../../../Configuration/sample_names.hpp"

#include <string>
#include <string_view>

namespace zlink::samples::tictactoe
{

struct room_route_t
{
    std::string route_channel_id;
    std::string owner_node_rid;
    std::string spot_rid;
    std::string spot_kind = sample_names_t::match_spot;
};

class room_route_store_t
{
  public:
    virtual ~room_route_store_t () = default;

    virtual void save (const room_route_t &route) = 0;
    virtual room_route_t require (std::string_view spot_rid) = 0;
};

} // namespace zlink::samples::tictactoe
