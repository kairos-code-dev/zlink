/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_names.hpp"
#include "../../Shared/Contracts/messages.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace zlink::samples::gamequest
{

class game_quest_state_t
{
  public:
    subscribe_quest_res_t subscribe_quest (const std::string &player_id)
    {
        const std::lock_guard lock (_mutex);
        _bindings.insert (player_id);
        _evidence.push_back (player_id + ":subscribed");
        return {visible_progress_unlocked (player_id)};
    }

    quest_event_msg_t enter_area (const enter_area_req_t &request)
    {
        return record_event (request.player_id, request.idempotency_key,
                             request.area_id == "ruins" ? "ruins-explorer" : request.area_id, 1, 1,
                             "area:" + request.area_id);
    }

    quest_event_msg_t kill_monster (const kill_monster_req_t &request)
    {
        return increment_event (request.player_id, request.idempotency_key,
                                sample_names_t::first_hunt, 1, 3,
                                "kill:" + request.monster_id);
    }

    quest_event_msg_t collect_item (const collect_item_req_t &request)
    {
        return increment_event (request.player_id, request.idempotency_key,
                                sample_names_t::herb_gathering, request.count, 5,
                                "item:" + request.item_id);
    }

    quest_event_msg_t complete_mission (const complete_mission_req_t &request)
    {
        const std::lock_guard lock (_mutex);
        _completed_missions[request.player_id].insert (request.mission_id);
        return record_event_unlocked (request.player_id, request.idempotency_key,
                                      request.mission_id, 1, 1,
                                      "mission:" + request.mission_id);
    }

    quest_event_msg_t unlock_feature (const unlock_feature_req_t &request)
    {
        const std::lock_guard lock (_mutex);
        _unlocked_features[request.player_id].insert (request.feature_id);
        return record_event_unlocked (request.player_id, request.idempotency_key,
                                      sample_names_t::open_auction, 1, 1,
                                      "feature:" + request.feature_id);
    }

    get_quest_progress_res_t get_progress (const std::string &player_id) const
    {
        const std::lock_guard lock (_mutex);
        return {visible_progress_unlocked (player_id)};
    }

    sync_quest_progress_res_t sync_progress (const std::string &player_id)
    {
        const std::lock_guard lock (_mutex);
        auto *first_hunt = find_progress_unlocked (player_id, sample_names_t::first_hunt);
        if (first_hunt != nullptr && first_hunt->current_count < 4) {
            upsert_progress_unlocked (player_id, sample_names_t::first_hunt, 4, 3,
                                      "sync-reconciled");
            _evidence.push_back (player_id + ":sync:first-hunt");
        }
        return {visible_progress_unlocked (player_id)};
    }

    get_gameplay_snapshot_res_t get_snapshot (const std::string &player_id) const
    {
        const std::lock_guard lock (_mutex);
        return {player_id,
                to_vector (_completed_missions, player_id),
                to_vector (_unlocked_features, player_id),
                to_vector (_entered_areas, player_id),
                _sequence};
    }

    quest_progress_t delete_projection (const delete_quest_projection_req_t &request)
    {
        const std::lock_guard lock (_mutex);
        _deleted_projections.insert (projection_key (request.player_id, request.quest_id));
        _evidence.push_back (request.player_id + ":" + request.quest_id + ":projection-deleted");
        const auto *progress = find_progress_unlocked (request.player_id, request.quest_id);
        return progress == nullptr ? quest_progress_t{} : *progress;
    }

    quest_progress_t rebuild_projection (const rebuild_quest_projection_req_t &request)
    {
        const std::lock_guard lock (_mutex);
        _deleted_projections.erase (projection_key (request.player_id, request.quest_id));
        const auto *progress = find_progress_unlocked (request.player_id, request.quest_id);
        if (progress == nullptr) {
            return {};
        }
        _evidence.push_back (request.player_id + ":" + request.quest_id + ":projection-rebuilt");
        return *progress;
    }

    game_quest_server_assert_res_t assert_server () const
    {
        const std::lock_guard lock (_mutex);
        const auto alice = visible_progress_unlocked ("player-alice");
        const auto bob = visible_progress_unlocked ("player-bob");
        const auto snapshot = get_snapshot_unlocked ("player-alice");
        const bool passed =
          any_progress (alice, sample_names_t::first_hunt, 4, "") &&
          any_progress (alice, sample_names_t::open_auction, 1,
                        sample_names_t::reward_granted) &&
          any_progress (bob, sample_names_t::herb_gathering, 5,
                        sample_names_t::reward_granted) &&
          contains (snapshot.unlocked_feature_ids, "auction") &&
          contains_substring (_evidence, "projection-rebuilt") &&
          contains_substring (_evidence, "sync:first-hunt");
        return {passed, _evidence};
    }

  private:
    quest_event_msg_t increment_event (const std::string &player_id,
                                       const std::string &idempotency_key,
                                       const std::string &quest_id,
                                       int delta,
                                       int required_count,
                                       const std::string &evidence)
    {
        const std::lock_guard lock (_mutex);
        auto *current = find_progress_unlocked (player_id, quest_id);
        const auto next = (current == nullptr ? 0 : current->current_count) + delta;
        return record_event_unlocked (player_id, idempotency_key, quest_id, next, required_count,
                                      evidence);
    }

    quest_event_msg_t record_event (const std::string &player_id,
                                    const std::string &idempotency_key,
                                    const std::string &quest_id,
                                    int current_count,
                                    int required_count,
                                    const std::string &evidence)
    {
        const std::lock_guard lock (_mutex);
        return record_event_unlocked (player_id, idempotency_key, quest_id, current_count,
                                      required_count, evidence);
    }

    quest_event_msg_t record_event_unlocked (const std::string &player_id,
                                             const std::string &idempotency_key,
                                             const std::string &quest_id,
                                             int current_count,
                                             int required_count,
                                             const std::string &evidence)
    {
        const auto event_id = player_id + "-" + idempotency_key;
        if (_idempotency.insert (idempotency_key).second) {
            upsert_progress_unlocked (player_id, quest_id, current_count, required_count, event_id);
            _evidence.push_back (event_id + ":" + evidence);
        }
        return {event_id};
    }

    quest_progress_t upsert_progress_unlocked (const std::string &player_id,
                                               const std::string &quest_id,
                                               int current_count,
                                               int required_count,
                                               const std::string &event_id)
    {
        ++_sequence;
        quest_progress_t progress{player_id,
                                  quest_id,
                                  current_count >= required_count
                                    ? sample_names_t::reward_granted
                                    : sample_names_t::active,
                                  current_count,
                                  required_count,
                                  event_id,
                                  _sequence};
        _projections[player_id][quest_id] = progress;
        return progress;
    }

    std::vector<quest_progress_t> visible_progress_unlocked (const std::string &player_id) const
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

    quest_progress_t *find_progress_unlocked (const std::string &player_id,
                                              const std::string &quest_id)
    {
        const auto player = _projections.find (player_id);
        if (player == _projections.end ()) {
            return nullptr;
        }
        const auto quest = player->second.find (quest_id);
        return quest == player->second.end () ? nullptr : &quest->second;
    }

    const quest_progress_t *find_progress_unlocked (const std::string &player_id,
                                                    const std::string &quest_id) const
    {
        const auto player = _projections.find (player_id);
        if (player == _projections.end ()) {
            return nullptr;
        }
        const auto quest = player->second.find (quest_id);
        return quest == player->second.end () ? nullptr : &quest->second;
    }

    get_gameplay_snapshot_res_t get_snapshot_unlocked (const std::string &player_id) const
    {
        return {player_id,
                to_vector (_completed_missions, player_id),
                to_vector (_unlocked_features, player_id),
                to_vector (_entered_areas, player_id),
                _sequence};
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
        return found == map.end () ? std::vector<std::string>{}
                                   : std::vector<std::string>{found->second.begin (),
                                                              found->second.end ()};
    }

    mutable std::mutex _mutex;
    std::set<std::string> _idempotency;
    std::set<std::string> _bindings;
    std::map<std::string, std::map<std::string, quest_progress_t>> _projections;
    std::set<std::string> _deleted_projections;
    std::map<std::string, std::set<std::string>> _completed_missions;
    std::map<std::string, std::set<std::string>> _unlocked_features;
    std::map<std::string, std::set<std::string>> _entered_areas;
    std::vector<std::string> _evidence;
    long long _sequence{0};
};

} // namespace zlink::samples::gamequest
