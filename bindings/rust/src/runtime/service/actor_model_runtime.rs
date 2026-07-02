use super::*;

// ---------------------------------------------------------------------------
// Actor values
// ---------------------------------------------------------------------------

impl ActorRef {
    pub(crate) fn from_raw(raw: &ffi::zlink_actor_ref_t) -> Self {
        Self {
            node_rid: RoutingId::from_raw(raw.node_rid),
            actor_id: unsafe { CStr::from_ptr(raw.actor_id.as_ptr()) }
                .to_string_lossy()
                .into_owned(),
            generation: raw.generation,
        }
    }

    pub(crate) fn to_raw(&self) -> Result<ffi::zlink_actor_ref_t, ConfigError> {
        let mut raw = ffi::zlink_actor_ref_t {
            node_rid: *self.node_rid.as_raw(),
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: self.generation,
        };
        write_c_array_config(&mut raw.actor_id, &self.actor_id, "actor_id")?;
        Ok(raw)
    }
}

fn routing_id_option(raw: ffi::zlink_routing_id_t) -> Option<RoutingId> {
    if raw.size == 0 {
        None
    } else {
        Some(RoutingId::from_raw(raw))
    }
}

fn routing_id_option_to_raw(value: Option<RoutingId>) -> ffi::zlink_routing_id_t {
    value
        .map(|rid| *rid.as_raw())
        .unwrap_or(ffi::zlink_routing_id_t::empty())
}

pub(super) fn actor_join_info_to_raw(info: &ActorJoinInfo) -> ffi::zlink_actor_join_info_t {
    ffi::zlink_actor_join_info_t {
        source_actor: info
            .source_actor
            .to_raw()
            .expect("native actor join source actor must remain valid"),
        target_actor: info
            .target_actor
            .to_raw()
            .expect("native actor join target actor must remain valid"),
        source_node_rid: *info.source_node_rid.as_raw(),
        source_spot_rid: routing_id_option_to_raw(info.source_spot_rid),
        target_node_rid: routing_id_option_to_raw(info.target_node_rid),
        target_spot_rid: routing_id_option_to_raw(info.target_spot_rid),
        join_epoch: info.join_epoch,
        request: info.request_cookie as *mut c_void,
        flags: info.flags,
    }
}

pub(super) fn routing_id_from_handle(handle: *mut c_void) -> Result<RoutingId, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
    check_config_rc(unsafe { ffi::zlink_get_routing_id(handle, raw.as_mut_ptr()) })?;
    Ok(RoutingId::from_raw(unsafe { raw.assume_init() }))
}

impl ActorRoute {
    pub(super) fn from_raw(raw: &ffi::zlink_actor_route_t) -> Self {
        Self {
            actor: ActorRef::from_raw(&raw.actor),
            current_spot_rid: RoutingId::from_raw(raw.current_spot_rid),
            current_spot_kind: SpotKind::from_raw(raw.current_spot_kind),
        }
    }
}

impl ActorRecvInfo {
    pub(super) fn from_raw(raw: &ffi::zlink_actor_recv_info_t) -> Self {
        Self {
            actor: ActorRef::from_raw(&raw.actor),
            source_node_rid: RoutingId::from_raw(raw.source_node_rid),
            source_session_rid: RoutingId::from_raw(raw.source_session_rid),
            flags: raw.flags,
        }
    }
}

impl ActorJoinInfo {
    pub(super) fn from_raw(raw: ffi::zlink_actor_join_info_t) -> Self {
        let source_actor = ActorRef::from_raw(&raw.source_actor);
        let target_actor = ActorRef::from_raw(&raw.target_actor);
        Self {
            actor: source_actor.clone(),
            source_actor,
            target_actor,
            source_node_rid: RoutingId::from_raw(raw.source_node_rid),
            source_spot_rid: routing_id_option(raw.source_spot_rid),
            target_node_rid: routing_id_option(raw.target_node_rid),
            target_spot_rid: routing_id_option(raw.target_spot_rid),
            join_epoch: raw.join_epoch,
            flags: raw.flags,
            request_cookie: raw.request as usize,
        }
    }
}

impl SpotActorLifecycleInfo {
    pub(super) fn from_raw(raw: ffi::zlink_spot_actor_lifecycle_info_t) -> Self {
        Self {
            previous_actor: ActorRef::from_raw(&raw.previous_actor),
            current_actor: ActorRef::from_raw(&raw.current_actor),
            previous_spot_rid: routing_id_option(raw.previous_spot_rid),
            current_spot_rid: routing_id_option(raw.current_spot_rid),
            join_epoch: raw.join_epoch,
            flags: raw.flags,
        }
    }
}

impl SpotActorLifecycleEventKind {
    pub(super) fn from_raw(raw: ffi::zlink_spot_actor_lifecycle_event_kind_t) -> Self {
        match raw {
            ffi::zlink_spot_actor_lifecycle_event_kind_t::ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT => {
                Self::Left
            }
            _ => Self::Joined,
        }
    }
}

impl SpotNodeSpotEntry {
    pub(super) fn from_raw(raw: &ffi::zlink_spot_node_spot_entry_t) -> Self {
        Self {
            spot_rid: RoutingId::from_raw(raw.spot_rid),
            spot_kind: SpotKind::from_raw(raw.spot_kind),
            dispatch_handler_attached: raw.dispatch_handler_attached != 0,
            joined_actor_count: raw.joined_actor_count,
            pending_actor_join_count: raw.pending_actor_join_count,
            route_synced: raw.route_synced != 0,
            last_changed_ms: raw.last_changed_ms,
        }
    }
}

impl SpotNodeActorEntry {
    pub(super) fn from_raw(raw: &ffi::zlink_spot_node_actor_entry_t) -> Self {
        Self {
            actor: ActorRef::from_raw(&raw.actor),
            current_spot_rid: RoutingId::from_raw(raw.current_spot_rid),
            current_spot_kind: SpotKind::from_raw(raw.current_spot_kind),
            route_synced: raw.route_synced != 0,
            pending_message_count: raw.pending_message_count,
            last_changed_ms: raw.last_changed_ms,
        }
    }
}

pub(super) struct NativeActor {
    pub(super) node_handle: *mut c_void,
    pub(super) actor_ref: Option<ActorRef>,
}

unsafe impl Send for NativeActor {}

impl ActorRuntime for NativeActor {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

pub(super) fn actor_native(actor: &Actor) -> &NativeActor {
    actor
        .inner
        .as_any()
        .downcast_ref::<NativeActor>()
        .expect("zlink native actor")
}

pub(super) fn actor_native_mut(actor: &mut Actor) -> &mut NativeActor {
    actor
        .inner
        .as_any_mut()
        .downcast_mut::<NativeActor>()
        .expect("zlink native actor")
}
