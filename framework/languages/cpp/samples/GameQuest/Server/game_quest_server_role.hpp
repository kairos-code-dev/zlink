/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace zlink::samples::gamequest
{

struct quest_ids_t
{
    static constexpr const char *first_hunt = "first-hunt";
    static constexpr const char *herb_gathering = "herb-gathering";
    static constexpr const char *open_auction = "open-auction";
    static constexpr const char *ruins_explorer = "ruins-explorer";
};

class game_quest_server_role_t
{
  public:
    subscribe_quest_res_t subscribe_quest (const std::string &player_id)
    {
        _evidence.push_back (player_id + ":subscribed");
        return {visible_progress (player_id)};
    }

    event_res_t enter_area (const enter_area_req_t &request)
    {
        const auto event_id = make_event_id (request.player_id, request.idempotency_key);
        if (remember (request.idempotency_key, event_id)) {
            _entered_areas[request.player_id].insert (request.area_id);
            upsert_progress (request.player_id,
                             request.area_id == "ruins" ? quest_ids_t::ruins_explorer
                                                         : request.area_id,
                             1,
                             1,
                             event_id);
            _evidence.push_back (event_id + ":area:" + request.area_id);
        }
        return {event_id};
    }

    event_res_t kill_monster (const kill_monster_req_t &request)
    {
        const auto event_id = make_event_id (request.player_id, request.idempotency_key);
        if (remember (request.idempotency_key, event_id)) {
            increment_progress (request.player_id, quest_ids_t::first_hunt, 1, 3, event_id);
            _evidence.push_back (event_id + ":kill:" + request.monster_id);
        }
        return {event_id};
    }

    event_res_t collect_item (const collect_item_req_t &request)
    {
        const auto event_id = make_event_id (request.player_id, request.idempotency_key);
        if (remember (request.idempotency_key, event_id)) {
            increment_progress (request.player_id, quest_ids_t::herb_gathering,
                                request.count, 5, event_id);
            _evidence.push_back (event_id + ":item:" + request.item_id);
        }
        return {event_id};
    }

    event_res_t complete_mission (const complete_mission_req_t &request)
    {
        const auto event_id = make_event_id (request.player_id, request.idempotency_key);
        if (remember (request.idempotency_key, event_id)) {
            _completed_missions[request.player_id].insert (request.mission_id);
            upsert_progress (request.player_id, request.mission_id, 1, 1, event_id);
            _evidence.push_back (event_id + ":mission:" + request.mission_id);
        }
        return {event_id};
    }

    event_res_t unlock_feature (const unlock_feature_req_t &request)
    {
        const auto event_id = make_event_id (request.player_id, request.idempotency_key);
        if (remember (request.idempotency_key, event_id)) {
            _unlocked_features[request.player_id].insert (request.feature_id);
            upsert_progress (request.player_id, quest_ids_t::open_auction, 1, 1, event_id);
            _evidence.push_back (event_id + ":feature:" + request.feature_id);
        }
        return {event_id};
    }

    get_quest_progress_res_t get_progress (const std::string &player_id) const
    {
        return {visible_progress (player_id)};
    }

    sync_quest_progress_res_t sync_progress (const std::string &player_id)
    {
        auto *first_hunt = find_progress (player_id, quest_ids_t::first_hunt);
        if (first_hunt != nullptr && first_hunt->current_count < 4) {
            upsert_progress (player_id, quest_ids_t::first_hunt, 4, 3, "sync-reconciled");
            _evidence.push_back (player_id + ":sync:first-hunt");
        }
        return {visible_progress (player_id)};
    }

    get_gameplay_snapshot_res_t get_snapshot (const std::string &player_id) const
    {
        return {player_id,
                to_vector (_completed_missions, player_id),
                to_vector (_unlocked_features, player_id),
                to_vector (_entered_areas, player_id),
                _sequence};
    }

    quest_progress_t delete_projection (const delete_quest_projection_req_t &request)
    {
        _deleted_projections.insert (projection_key (request.player_id, request.quest_id));
        _evidence.push_back (request.player_id + ":" + request.quest_id + ":projection-deleted");
        const auto *progress = find_progress (request.player_id, request.quest_id);
        if (progress == nullptr) {
            return {};
        }
        return *progress;
    }

    quest_progress_t rebuild_projection (const rebuild_quest_projection_req_t &request)
    {
        _deleted_projections.erase (projection_key (request.player_id, request.quest_id));
        const auto *progress = find_progress (request.player_id, request.quest_id);
        if (progress == nullptr) {
            throw std::logic_error ("unknown projection");
        }
        _evidence.push_back (request.player_id + ":" + request.quest_id + ":projection-rebuilt");
        return *progress;
    }

    game_quest_server_assert_res_t assert_server () const
    {
        const auto alice = visible_progress ("player-alice");
        const auto bob = visible_progress ("player-bob");
        const auto snapshot = get_snapshot ("player-alice");
        const bool passed =
          any_progress (alice, quest_ids_t::first_hunt, 4, "") &&
          any_progress (alice, quest_ids_t::open_auction, 1, quest_status_t::reward_granted) &&
          any_progress (bob, quest_ids_t::herb_gathering, 5, quest_status_t::reward_granted) &&
          contains (snapshot.unlocked_feature_ids, "auction") &&
          contains_substring (_evidence, "projection-rebuilt") &&
          contains_substring (_evidence, "sync:first-hunt");
        return {passed, _evidence};
    }

  private:
    quest_progress_t increment_progress (const std::string &player_id,
                                         const std::string &quest_id,
                                         int delta,
                                         int required_count,
                                         const std::string &event_id)
    {
        const auto *current = find_progress (player_id, quest_id);
        const int next_count = (current == nullptr ? 0 : current->current_count) + delta;
        return upsert_progress (player_id, quest_id, next_count, required_count, event_id);
    }

    quest_progress_t upsert_progress (const std::string &player_id,
                                      const std::string &quest_id,
                                      int current_count,
                                      int required_count,
                                      const std::string &event_id)
    {
        ++_sequence;
        quest_progress_t progress{player_id,
                                  quest_id,
                                  current_count >= required_count
                                    ? quest_status_t::reward_granted
                                    : quest_status_t::in_progress,
                                  current_count,
                                  required_count,
                                  event_id,
                                  _sequence};
        _projections[player_id][quest_id] = progress;
        return progress;
    }

    std::vector<quest_progress_t> visible_progress (const std::string &player_id) const
    {
        std::vector<quest_progress_t> values;
        const auto found = _projections.find (player_id);
        if (found == _projections.end ()) {
            return values;
        }
        for (const auto &[quest_id, progress] : found->second) {
            if (_deleted_projections.find (projection_key (player_id, quest_id)) ==
                _deleted_projections.end ()) {
                values.push_back (progress);
            }
        }
        return values;
    }

    quest_progress_t *find_progress (const std::string &player_id, const std::string &quest_id)
    {
        const auto player = _projections.find (player_id);
        if (player == _projections.end ()) {
            return nullptr;
        }
        const auto quest = player->second.find (quest_id);
        return quest == player->second.end () ? nullptr : &quest->second;
    }

    const quest_progress_t *find_progress (const std::string &player_id,
                                           const std::string &quest_id) const
    {
        const auto player = _projections.find (player_id);
        if (player == _projections.end ()) {
            return nullptr;
        }
        const auto quest = player->second.find (quest_id);
        return quest == player->second.end () ? nullptr : &quest->second;
    }

    bool remember (const std::string &idempotency_key, const std::string &event_id)
    {
        if (_idempotency.find (idempotency_key) != _idempotency.end ()) {
            return false;
        }
        _idempotency.emplace (idempotency_key, event_id);
        return true;
    }

    static std::string make_event_id (const std::string &player_id,
                                      const std::string &idempotency_key)
    {
        return player_id + "-" + idempotency_key;
    }

    static std::string projection_key (const std::string &player_id, const std::string &quest_id)
    {
        return player_id + "/" + quest_id;
    }

    static bool contains (const std::vector<std::string> &values, const std::string &needle)
    {
        return std::find (values.begin (), values.end (), needle) != values.end ();
    }

    static bool contains_substring (const std::vector<std::string> &values,
                                    const std::string &needle)
    {
        return std::any_of (values.begin (), values.end (), [&] (const std::string &value) {
            return value.find (needle) != std::string::npos;
        });
    }

    static bool any_progress (const std::vector<quest_progress_t> &values,
                              const std::string &quest_id,
                              int min_count,
                              const std::string &status)
    {
        return std::any_of (values.begin (), values.end (), [&] (const quest_progress_t &value) {
            return value.quest_id == quest_id && value.current_count >= min_count &&
                   (status.empty () || value.status == status);
        });
    }

    static std::vector<std::string> to_vector (
      const std::map<std::string, std::set<std::string>> &map,
      const std::string &key)
    {
        const auto found = map.find (key);
        if (found == map.end ()) {
            return {};
        }
        return {found->second.begin (), found->second.end ()};
    }

    std::map<std::string, std::string> _idempotency;
    std::map<std::string, std::map<std::string, quest_progress_t>> _projections;
    std::set<std::string> _deleted_projections;
    std::map<std::string, std::set<std::string>> _completed_missions;
    std::map<std::string, std::set<std::string>> _unlocked_features;
    std::map<std::string, std::set<std::string>> _entered_areas;
    std::vector<std::string> _evidence;
    long long _sequence{0};
};

} // namespace zlink::samples::gamequest
