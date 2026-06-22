use super::*;
use crate::runtime_bridge::SpotNodePublicRuntime;

// ---------------------------------------------------------------------------
// SpotNode
// ---------------------------------------------------------------------------

struct NativeSpotNode {
    ctx_handle: *mut c_void,
    handle: *mut c_void,
}

unsafe impl Send for NativeSpotNode {}

impl SpotNodeRuntime for NativeSpotNode {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

pub(crate) fn spot_node_handle(node: &SpotNode) -> *mut c_void {
    node.inner
        .as_any()
        .downcast_ref::<NativeSpotNode>()
        .expect("zlink native spot node")
        .handle
}

pub(crate) fn spot_node_context_handle(node: &SpotNode) -> *mut c_void {
    node.inner
        .as_any()
        .downcast_ref::<NativeSpotNode>()
        .expect("zlink native spot node")
        .ctx_handle
}

fn spot_node_handle_mut(node: &mut SpotNode) -> &mut *mut c_void {
    &mut node
        .inner
        .as_any_mut()
        .downcast_mut::<NativeSpotNode>()
        .expect("zlink native spot node")
        .handle
}

fn set_spot_node_option_i32(
    node: &SpotNode,
    option: ffi::zlink_spot_node_option_t,
    value: i32,
) -> Result<(), ConfigError> {
    check_config_rc(unsafe {
        ffi::zlink_set_spot_node_option(
            spot_node_handle(node),
            option,
            (&value as *const i32).cast(),
            std::mem::size_of::<i32>(),
        )
    })
}

fn get_spot_node_option_i32(
    node: &SpotNode,
    option: ffi::zlink_spot_node_option_t,
) -> Result<i32, ConfigError> {
    let mut value: i32 = 0;
    let mut len = std::mem::size_of::<i32>();
    check_config_rc(unsafe {
        ffi::zlink_get_spot_node_option(
            spot_node_handle(node),
            option,
            (&mut value as *mut i32).cast(),
            &mut len,
        )
    })?;
    Ok(value)
}

impl SpotNodePublicRuntime for SpotNode {
    fn new(ctx: &crate::core_context::Context) -> Result<Self, ConfigError> {
        let ctx_handle = crate::ctx::context_handle(ctx);
        let handle = unsafe { ffi::zlink_spot_node_new(ctx_handle, std::ptr::null()) };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self {
            inner: Box::new(NativeSpotNode { ctx_handle, handle }),
        })
    }

    fn new_with_options(
        ctx: &crate::core_context::Context,
        options: SpotNodeOptions,
    ) -> Result<Self, ConfigError> {
        let raw_options = ffi::zlink_spot_node_options_t {
            mode: options.mode.unwrap_or(SpotNodeMode::All).to_raw(),
        };
        let ctx_handle = crate::ctx::context_handle(ctx);
        let handle = unsafe {
            ffi::zlink_spot_node_new(
                ctx_handle,
                (&raw_options as *const ffi::zlink_spot_node_options_t).cast(),
            )
        };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self {
            inner: Box::new(NativeSpotNode { ctx_handle, handle }),
        })
    }

    fn set_pub_bind(&self, endpoint: &str) -> Result<(), ConfigError> {
        let c = CString::new(endpoint).map_err(|_| {
            ConfigError::new(crate::error::ConfigResult::InvalidArgument, libc::EINVAL)
        })?;
        check_config_rc(unsafe {
            ffi::zlink_spot_node_set_pub_bind(spot_node_handle(self), c.as_ptr())
        })
    }

    fn set_router_bind(&self, endpoint: &str) -> Result<(), ConfigError> {
        let c = CString::new(endpoint).map_err(|_| {
            ConfigError::new(crate::error::ConfigResult::InvalidArgument, libc::EINVAL)
        })?;
        check_config_rc(unsafe {
            ffi::zlink_spot_node_set_router_bind(spot_node_handle(self), c.as_ptr())
        })
    }

    fn last_endpoint(&self) -> Result<String, ConfigError> {
        Ok(self.status()?.local_endpoint)
    }

    fn connect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError> {
        let c = CString::new(peer_endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe {
            ffi::zlink_spot_node_connect_peer(spot_node_handle(self), c.as_ptr())
        })
    }

    fn disconnect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError> {
        let c = CString::new(peer_endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe {
            ffi::zlink_spot_node_disconnect_peer(spot_node_handle(self), c.as_ptr())
        })
    }

    fn disconnect_peer_rid(&self, target_node_rid: &RoutingId) -> Result<(), ConnectError> {
        check_connect_rc(unsafe {
            ffi::zlink_spot_node_disconnect_peer_rid(
                spot_node_handle(self),
                target_node_rid.as_raw(),
            )
        })
    }

    fn connect_router_channel_peer(
        &self,
        channel_name: &str,
        endpoint: &str,
    ) -> Result<(), ConnectError> {
        let channel = CString::new(channel_name).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        let ep = CString::new(endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe {
            ffi::zlink_spot_node_connect_router_channel_peer(
                spot_node_handle(self),
                channel.as_ptr(),
                ep.as_ptr(),
            )
        })
    }

    fn connect_router_channel_peer_rid(
        &self,
        channel_name: &str,
        peer_rid: &RoutingId,
        endpoint: &str,
    ) -> Result<(), ConnectError> {
        let channel = CString::new(channel_name).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        let ep = CString::new(endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe {
            ffi::zlink_spot_node_connect_router_channel_peer_rid(
                spot_node_handle(self),
                channel.as_ptr(),
                peer_rid.as_raw(),
                ep.as_ptr(),
            )
        })
    }

    fn disconnect_router_channel_peer(
        &self,
        channel_name: &str,
        endpoint: &str,
    ) -> Result<(), ConnectError> {
        let channel = CString::new(channel_name).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        let ep = CString::new(endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe {
            ffi::zlink_spot_node_disconnect_router_channel_peer(
                spot_node_handle(self),
                channel.as_ptr(),
                ep.as_ptr(),
            )
        })
    }

    fn disconnect_router_channel_peer_rid(
        &self,
        channel_name: &str,
        peer_rid: &RoutingId,
    ) -> Result<(), ConnectError> {
        let channel = CString::new(channel_name).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe {
            ffi::zlink_spot_node_disconnect_router_channel_peer_rid(
                spot_node_handle(self),
                channel.as_ptr(),
                peer_rid.as_raw(),
            )
        })
    }

    fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_spot_node_attach_discovery(spot_node_handle(self), discovery.raw())
        })
    }

    fn attach_spot_route_channel_discovery(
        &self,
        channel_name: &str,
        discovery: &Discovery,
    ) -> Result<(), ConfigError> {
        let channel = CString::new(channel_name).map_err(|_| {
            ConfigError::new(crate::error::ConfigResult::InvalidArgument, libc::EINVAL)
        })?;
        check_config_rc(unsafe {
            ffi::zlink_spot_node_attach_router_channel_discovery(
                spot_node_handle(self),
                channel.as_ptr(),
                discovery.raw(),
            )
        })
    }

    fn attach_pub_ingress(&self, pub_sock: &crate::PubSocket) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_spot_node_attach_pub_ingress(
                spot_node_handle(self),
                crate::socket::pub_inner(pub_sock).handle,
            )
        })
    }

    fn router_high_water_mark(&self) -> Result<i32, ConfigError> {
        get_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_ROUTER_HWM,
        )
    }

    fn set_router_high_water_mark(&self, value: i32) -> Result<(), ConfigError> {
        set_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_ROUTER_HWM,
            value,
        )
    }

    fn pubsub_high_water_mark(&self) -> Result<i32, ConfigError> {
        get_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_PUBSUB_HWM,
        )
    }

    fn set_pubsub_high_water_mark(&self, value: i32) -> Result<(), ConfigError> {
        set_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_PUBSUB_HWM,
            value,
        )
    }

    fn router_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        let raw = get_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE,
        )?;
        AutoHwmProfile::from_raw(raw)
    }

    fn set_router_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        set_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE,
            profile.to_raw(),
        )
    }

    fn pubsub_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        let raw = get_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE,
        )?;
        AutoHwmProfile::from_raw(raw)
    }

    fn set_pubsub_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        set_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE,
            profile.to_raw(),
        )
    }

    fn dispatch_workers_min(&self) -> Result<i32, ConfigError> {
        get_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN,
        )
    }

    fn set_dispatch_workers_min(&self, value: i32) -> Result<(), ConfigError> {
        set_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN,
            value,
        )
    }

    fn dispatch_workers_max(&self) -> Result<i32, ConfigError> {
        get_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX,
        )
    }

    fn set_dispatch_workers_max(&self, value: i32) -> Result<(), ConfigError> {
        set_spot_node_option_i32(
            self,
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX,
            value,
        )
    }

    fn set_tls_server(
        &self,
        cert_pem: &str,
        key_pem: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        set_tls_server_config(
            spot_node_handle(self),
            cert_pem,
            key_pem,
            require_client_cert,
        )
    }

    fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        set_tls_client_config(spot_node_handle(self), ca_cert_pem, hostname, trust_system)
    }

    fn create_actor(&self, actor_id: &str) -> Result<Actor, ConfigError> {
        let c_actor_id = fixed_cstring_config(actor_id, "actor_id")?;
        let mut raw = MaybeUninit::<ffi::zlink_actor_ref_t>::zeroed();
        check_config_rc(unsafe {
            ffi::zlink_spot_node_actor_new(
                spot_node_handle(self),
                c_actor_id.as_ptr(),
                raw.as_mut_ptr(),
            )
        })?;
        Ok(Actor {
            inner: Box::new(NativeActor {
                node_handle: spot_node_handle(self),
                actor_ref: Some(ActorRef::from_raw(&unsafe { raw.assume_init() })),
            }),
        })
    }

    fn actor_lookup(&self, actor_id: &str) -> Result<ActorRef, ConfigError> {
        let c_actor_id = fixed_cstring_config(actor_id, "actor_id")?;
        let mut raw = MaybeUninit::<ffi::zlink_actor_ref_t>::zeroed();
        check_config_rc(unsafe {
            ffi::zlink_spot_node_actor_lookup(
                spot_node_handle(self),
                c_actor_id.as_ptr(),
                raw.as_mut_ptr(),
            )
        })?;
        Ok(ActorRef::from_raw(&unsafe { raw.assume_init() }))
    }

    /// Build an unchecked remote Actor ref (generation == 0) from a target node rid + id.
    fn remote_actor_ref(
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> Result<ActorRef, ConfigError> {
        let c_actor_id = fixed_cstring_config(actor_id, "actor_id")?;
        let mut raw = ffi::zlink_actor_ref_t {
            node_rid: *target_node_rid.as_raw(),
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: 0,
        };
        let bytes = c_actor_id.as_bytes_with_nul();
        for (idx, byte) in bytes.iter().enumerate().take(raw.actor_id.len()) {
            raw.actor_id[idx] = *byte as c_char;
        }
        Ok(ActorRef::from_raw(&raw))
    }

    /// Async remote Actor lookup (operation builder).
    fn remote_actor_get_ref(
        &self,
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> ActorLookupOp<Empty> {
        let c_actor_id = fixed_cstring_or_panic(actor_id, "actor_id");
        wrap_actor_lookup_op(NativeActorLookupOp {
            node_handle: spot_node_handle(self),
            target_node_rid: *target_node_rid,
            actor_id: c_actor_id,
            timeout: Duration::ZERO,
        })
    }

    /// Async destroy (operation builder).
    fn destroy_actor(&self, actor: &ActorRef) -> ActorDestroyOp<Empty> {
        let raw = actor.to_raw().unwrap_or(ffi::zlink_actor_ref_t {
            node_rid: ffi::zlink_routing_id_t::empty(),
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: 0,
        });
        wrap_actor_reply_op(NativeActorReplyOp {
            handle: spot_node_handle(self),
            kind: NativeActorReplyOpKind::Destroy { actor: raw },
            timeout: Duration::ZERO,
        })
    }

    /// Async user-Spot join (operation builder). Payload accumulates via `.message(...)`.
    fn join_actor(
        &self,
        actor: &ActorRef,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
    ) -> ActorJoinOp<Empty> {
        let raw = actor.to_raw().unwrap_or(ffi::zlink_actor_ref_t {
            node_rid: ffi::zlink_routing_id_t::empty(),
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: 0,
        });
        wrap_actor_join_op(NativeActorJoinOp {
            node_handle: spot_node_handle(self),
            spot_handle: std::ptr::null_mut(),
            actor: raw,
            dest_node_rid: *dest_node_rid,
            dest_spot_rid: *dest_spot_rid,
            parts: Vec::new(),
            flags: SendFlags::NONE,
            timeout: Duration::ZERO,
        })
    }

    /// Async Entry Spot join (operation builder). The target is the Entry Spot
    /// of `dest_node_rid`; callers pass an explicit request message.
    fn join_actor_entry_spot(
        &self,
        actor: &ActorRef,
        dest_node_rid: &RoutingId,
        request: Message,
    ) -> ActorJoinEntrySpotOp<Ready> {
        let raw = actor.to_raw().unwrap_or(ffi::zlink_actor_ref_t {
            node_rid: ffi::zlink_routing_id_t::empty(),
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: 0,
        });
        wrap_actor_join_entry_spot_op(NativeActorJoinEntrySpotOp {
            node_handle: spot_node_handle(self),
            actor: raw,
            dest_node_rid: *dest_node_rid,
            parts: vec![request],
            flags: SendFlags::NONE,
            timeout: Duration::ZERO,
        })
    }

    /// Async leave to the same node's Entry Spot (operation builder).
    fn leave_actor(&self, actor: &ActorRef, current_spot_rid: &RoutingId) -> ActorLeaveOp<Empty> {
        let raw = actor.to_raw().unwrap_or(ffi::zlink_actor_ref_t {
            node_rid: ffi::zlink_routing_id_t::empty(),
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: 0,
        });
        wrap_actor_reply_op(NativeActorReplyOp {
            handle: spot_node_handle(self),
            kind: NativeActorReplyOpKind::Leave {
                actor: raw,
                current_spot_rid: *current_spot_rid,
            },
            timeout: Duration::ZERO,
        })
    }

    /// Actor-to-session relay (operation builder).
    fn send_bound_session_msg(&self, actor: &ActorRef) -> SendOp<Empty> {
        let raw = actor.to_raw().unwrap_or(ffi::zlink_actor_ref_t {
            node_rid: ffi::zlink_routing_id_t::empty(),
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: 0,
        });
        wrap_send_op(NativeSendOp {
            handle: spot_node_handle(self),
            kind: SendOpKind::ActorBoundSession { actor: raw },
            parts: Vec::new(),
            flags: SendFlags::NONE,
        })
    }

    fn status(&self) -> Result<SpotNodeStatus, ConfigError> {
        let mut raw = MaybeUninit::<ffi::zlink_spot_node_status_t>::uninit();
        check_config_rc(unsafe {
            ffi::zlink_spot_node_status(spot_node_handle(self), raw.as_mut_ptr())
        })?;
        let raw = unsafe { raw.assume_init() };
        Ok(SpotNodeStatus::from_raw(&raw))
    }

    fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_routing_id(
                spot_node_handle(self),
                rid.data().as_ptr() as *const c_void,
                rid.len(),
            )
        })
    }

    fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        let mut raw = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        check_config_rc(unsafe {
            ffi::zlink_get_routing_id(spot_node_handle(self), raw.as_mut_ptr())
        })?;
        Ok(RoutingId::from_raw(unsafe { raw.assume_init() }))
    }

    fn entry_spot(&self) -> Result<Spot, ConfigError> {
        let mut spot_handle: *mut c_void = ptr::null_mut();
        check_config_rc(unsafe {
            ffi::zlink_spot_node_entry_spot(spot_node_handle(self), &mut spot_handle)
        })?;
        if spot_handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(wrap_spot(spot_handle, spot_node_handle(self)))
    }

    fn create_spot(&self) -> Result<Spot, ConfigError> {
        Spot::new(self)
    }

    fn spot_lookup(&self, spot_rid: &RoutingId) -> Result<Option<Spot>, ConfigError> {
        let mut spot_handle: *mut c_void = ptr::null_mut();
        check_config_rc(unsafe {
            ffi::zlink_spot_node_spot_lookup(
                spot_node_handle(self),
                spot_rid.as_raw(),
                &mut spot_handle,
            )
        })?;
        if spot_handle.is_null() {
            return Ok(None);
        }
        Ok(Some(wrap_spot(spot_handle, spot_node_handle(self))))
    }

    fn get_or_create_spot(&self, spot_rid: &RoutingId) -> Result<(Spot, bool), ConfigError> {
        let mut spot_handle: *mut c_void = ptr::null_mut();
        let mut created: u32 = 0;
        check_config_rc(unsafe {
            ffi::zlink_spot_node_spot_get_or_new(
                spot_node_handle(self),
                spot_rid.as_raw(),
                &mut spot_handle,
                &mut created,
            )
        })?;
        Ok((wrap_spot(spot_handle, spot_node_handle(self)), created != 0))
    }

    fn peers(&self) -> Result<Vec<SpotNodePeerEntry>, ConfigError> {
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_spot_node_peers(spot_node_handle(self), ptr::null(), ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }

        let mut entries =
            vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_peer_entry_t>() }; count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_spot_node_peers(
                    spot_node_handle(self),
                    ptr::null(),
                    entries_ptr,
                    count_ptr,
                )
            },
            entries.as_mut_ptr(),
        )?;
        Ok(entries[..actual]
            .iter()
            .map(SpotNodePeerEntry::from_raw)
            .collect())
    }

    fn peers_query(
        &self,
        filter: &SpotNodePeerFilter,
    ) -> Result<Vec<SpotNodePeerEntry>, ConfigError> {
        with_spot_node_peer_filter_config(filter, |filter_ptr| {
            let count = count_entries_config(|count| unsafe {
                ffi::zlink_spot_node_peers(
                    spot_node_handle(self),
                    filter_ptr,
                    ptr::null_mut(),
                    count,
                )
            })?;
            if count == 0 {
                return Ok(Vec::new());
            }

            let mut entries =
                vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_peer_entry_t>() }; count];
            let actual = read_entries_config(
                count,
                |entries_ptr, count_ptr| unsafe {
                    ffi::zlink_spot_node_peers(
                        spot_node_handle(self),
                        filter_ptr,
                        entries_ptr,
                        count_ptr,
                    )
                },
                entries.as_mut_ptr(),
            )?;
            Ok(entries[..actual]
                .iter()
                .map(SpotNodePeerEntry::from_raw)
                .collect())
        })
    }

    fn subjects(
        &self,
        filter: Option<&SpotNodeSubjectFilter>,
    ) -> Result<Vec<SpotNodeSubjectEntry>, ConfigError> {
        self.subjects_query_opt(filter)
    }

    fn internal_sockets(
        &self,
        filter: Option<&SpotNodeSocketFilter>,
    ) -> Result<Vec<SpotNodeSocketEntry>, ConfigError> {
        self.internal_sockets_opt(filter)
    }

    fn spots(&self) -> Result<Vec<SpotNodeSpotEntry>, ConfigError> {
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_spot_node_spots(spot_node_handle(self), ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }
        let mut entries =
            vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_spot_entry_t>() }; count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_spot_node_spots(spot_node_handle(self), entries_ptr, count_ptr)
            },
            entries.as_mut_ptr(),
        )?;
        Ok(entries[..actual]
            .iter()
            .map(SpotNodeSpotEntry::from_raw)
            .collect())
    }

    fn actors(&self) -> Result<Vec<SpotNodeActorEntry>, ConfigError> {
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_spot_node_actors(spot_node_handle(self), ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }
        let mut entries =
            vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_actor_entry_t>() }; count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_spot_node_actors(spot_node_handle(self), entries_ptr, count_ptr)
            },
            entries.as_mut_ptr(),
        )?;
        Ok(entries[..actual]
            .iter()
            .map(SpotNodeActorEntry::from_raw)
            .collect())
    }

    fn close(&mut self) -> Result<(), CloseError> {
        destroy_handle_close(spot_node_handle_mut(self), ffi::zlink_spot_node_destroy)
    }
}

impl SpotNode {
    pub(crate) fn raw(&self) -> *mut c_void {
        spot_node_handle(self)
    }
}

impl Drop for SpotNode {
    fn drop(&mut self) {
        let _ = destroy_handle(spot_node_handle_mut(self), ffi::zlink_spot_node_destroy);
    }
}
