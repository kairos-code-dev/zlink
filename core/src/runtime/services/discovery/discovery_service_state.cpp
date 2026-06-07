/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/c_api_copy_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

#include <algorithm>
#include <cstring>

namespace zlink
{
namespace
{
static bool provider_equal_local (const provider_info_t &lhs_, const provider_info_t &rhs_)
{
    if (lhs_.endpoint != rhs_.endpoint)
        return false;
    if (lhs_.service_role != rhs_.service_role)
        return false;
    if (lhs_.weight != rhs_.weight)
        return false;
    if (lhs_.routing_id.size != rhs_.routing_id.size)
        return false;
    if (lhs_.routing_id.size > 0
        && memcmp (lhs_.routing_id.data, rhs_.routing_id.data, lhs_.routing_id.size) != 0) {
        return false;
    }
    return lhs_.value == rhs_.value && lhs_.metadata == rhs_.metadata
           && lhs_.source_registry == rhs_.source_registry
           && lhs_.registration_id == rhs_.registration_id;
}

static bool providers_equal_local (const std::vector<provider_info_t> &lhs_,
                                   const std::vector<provider_info_t> &rhs_)
{
    if (lhs_.size () != rhs_.size ())
        return false;
    for (size_t i = 0; i < lhs_.size (); ++i) {
        if (!provider_equal_local (lhs_[i], rhs_[i]))
            return false;
    }
    return true;
}

static bool provider_less_local (const provider_info_t &lhs_, const provider_info_t &rhs_)
{
    if (lhs_.service_role != rhs_.service_role)
        return lhs_.service_role < rhs_.service_role;
    return lhs_.endpoint < rhs_.endpoint;
}

}

discovery_service_change_t::discovery_service_change_t () : changed (false)
{
    memset (&event, 0, sizeof (event));
}

discovery_service_state_t::discovery_service_state_t () :
    _observer_callbacks_inflight (0), _update_seq (0), _service_seq (0)
{
}

int discovery_service_state_t::add_observer (discovery_observer_t *observer_)
{
    if (!observer_)
        return 0;
    _observers.insert (observer_);
    return 0;
}

int discovery_service_state_t::remove_observer (discovery_observer_t *observer_)
{
    if (!observer_)
        return 0;
    if (_observer_callbacks_inflight > 0) {
        errno = EBUSY;
        return -1;
    }
    _observers.erase (observer_);
    return 0;
}

bool discovery_service_state_t::has_inflight_observer_callbacks () const
{
    return _observer_callbacks_inflight > 0;
}

void discovery_service_state_t::begin_observer_notification (
  const std::string &channel_name_,
  const std::set<std::string> &services_,
  std::vector<discovery_observer_t *> *observers_out_)
{
    if (!observers_out_)
        return;
    observers_out_->clear ();
    if (services_.find (channel_name_) == services_.end () || _observers.empty ())
        return;
    observers_out_->assign (_observers.begin (), _observers.end ());
    _observer_callbacks_inflight += observers_out_->size ();
}

void discovery_service_state_t::finish_observer_notification (size_t observer_count_)
{
    if (observer_count_ > _observer_callbacks_inflight)
        _observer_callbacks_inflight = 0;
    else
        _observer_callbacks_inflight -= observer_count_;
}

void discovery_service_state_t::take_shutdown_observers (
  std::vector<discovery_observer_t *> *observers_out_)
{
    if (!observers_out_)
        return;
    observers_out_->assign (_observers.begin (), _observers.end ());
    _observers.clear ();
    _observer_callbacks_inflight = 0;
}

void discovery_service_state_t::snapshot_providers (std::vector<provider_info_t> *out_) const
{
    if (!out_)
        return;
    *out_ = _providers;
}

void discovery_service_state_t::snapshot_member_peers (
  zlink_auto_connect_type_t auto_connect_type_,
  const std::set<discovery_member_key_t> &local_members_,
  std::vector<zlink_member_peer_entry_t> *out_) const
{
    if (!out_)
        return;
    out_->clear ();
    out_->reserve (_providers.size ());

    for (size_t i = 0; i < _providers.size (); ++i) {
        const provider_info_t &provider = _providers[i];
        const discovery_member_key_t key (provider.service_role, provider.endpoint);
        if (local_members_.count (key) != 0)
            continue;

        zlink_member_peer_entry_t entry;
        memset (&entry, 0, sizeof (entry));
        entry.auto_connect_type = static_cast<zlink_auto_connect_type_t> (auto_connect_type_);
        entry.service_role = static_cast<zlink_service_role_t> (provider.service_role);
        copy_fixed_c_string_from_bytes (entry.channel_name, sizeof (entry.channel_name),
                                        provider.channel_name.data (),
                                        provider.channel_name.size ());
        copy_fixed_c_string_from_bytes (entry.endpoint, sizeof (entry.endpoint),
                                        provider.endpoint.data (), provider.endpoint.size ());
        entry.routing_id = provider.routing_id;
        entry.weight = provider.weight;
        entry.value = provider.value;
        out_->push_back (entry);
    }
}

uint64_t discovery_service_state_t::update_seq () const
{
    return _update_seq;
}

uint64_t discovery_service_state_t::service_update_seq () const
{
    return _service_seq;
}

void discovery_service_state_t::apply_provider_snapshot (
  uint32_t registry_id_,
  uint64_t list_seq_,
  const std::vector<provider_info_t> &updated_,
  const std::string &channel_name_,
  const zlink_routing_id_t &routing_id_,
  discovery_service_change_t *change_out_)
{
    if (change_out_)
        *change_out_ = discovery_service_change_t ();

    std::map<uint32_t, uint64_t>::iterator it = _registry_seq.find (registry_id_);
    if (it != _registry_seq.end () && list_seq_ <= it->second)
        return;
    _registry_seq[registry_id_] = list_seq_;

    std::vector<provider_info_t> sorted_updated = updated_;
    std::sort (sorted_updated.begin (), sorted_updated.end (), provider_less_local);

    if (providers_equal_local (_providers, sorted_updated)) {
        _providers.swap (sorted_updated);
        return;
    }

    const bool had_providers = !_providers.empty ();
    _service_seq = _update_seq + 1;
    _providers.swap (sorted_updated);
    ++_update_seq;

    if (!change_out_)
        return;

    change_out_->changed = true;
    change_out_->event.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
    change_out_->event.detail_flags =
      ZLINK_EVENT_DETAIL_CHANNEL_NAME | ZLINK_EVENT_DETAIL_SUBJECT_RID;
    change_out_->event.routing_id = routing_id_;
    copy_fixed_c_string_from_cstr (change_out_->event.channel_name,
                                   sizeof (change_out_->event.channel_name),
                                   channel_name_.c_str ());
    change_out_->event.value = static_cast<uint32_t> (_providers.size ());
    if (!had_providers)
        change_out_->event.event_type = ZLINK_DISCOVERY_SERVICE_UP;
    else if (_providers.empty ())
        change_out_->event.event_type = ZLINK_DISCOVERY_SERVICE_DOWN;
    else
        change_out_->event.event_type = ZLINK_DISCOVERY_PROVIDERS_CHANGED;
}
}
