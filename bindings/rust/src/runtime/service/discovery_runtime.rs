use super::*;

// ---------------------------------------------------------------------------
// AutoConnectType / ServiceRole newtypes
// ---------------------------------------------------------------------------

struct NativeDiscovery {
    handle: *mut c_void,
}

unsafe impl Send for NativeDiscovery {}

impl DiscoveryRuntime for NativeDiscovery {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

fn discovery_native(discovery: &Discovery) -> &NativeDiscovery {
    discovery
        .inner
        .as_any()
        .downcast_ref::<NativeDiscovery>()
        .expect("zlink native discovery")
}

fn discovery_native_mut(discovery: &mut Discovery) -> &mut NativeDiscovery {
    discovery
        .inner
        .as_any_mut()
        .downcast_mut::<NativeDiscovery>()
        .expect("zlink native discovery")
}

pub(crate) fn discovery_new(
    ctx: &crate::core_context::Context,
    auto_connect_type: AutoConnectType,
    channel_name: &str,
) -> Result<Discovery, ConfigError> {
    let c_name = fixed_cstring_config(channel_name, "channel_name")?;
    let handle = unsafe {
        ffi::zlink_discovery_new(
            crate::ctx::context_handle(ctx),
            auto_connect_type.to_raw(),
            c_name.as_ptr(),
        )
    };
    if handle.is_null() {
        return Err(ConfigError::new(
            crate::error::ConfigResult::InvalidHandle,
            last_errno(),
        ));
    }
    Ok(Discovery {
        inner: Box::new(NativeDiscovery { handle }),
    })
}

pub(crate) fn discovery_connect_registry(
    discovery: &Discovery,
    endpoint: &str,
) -> Result<(), ConnectError> {
    let c = CString::new(endpoint).map_err(|_| {
        ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
    })?;
    check_connect_rc(unsafe { ffi::zlink_discovery_connect_registry(discovery.raw(), c.as_ptr()) })
}

pub(crate) fn discovery_set_value(discovery: &Discovery, value: i64) -> Result<(), ConfigError> {
    check_config_rc(unsafe { ffi::zlink_discovery_set_value(discovery.raw(), value) })
}

pub(crate) fn discovery_get_value(discovery: &Discovery) -> Result<i64, ConfigError> {
    let mut v: i64 = 0;
    check_config_rc(unsafe { ffi::zlink_discovery_get_value(discovery.raw(), &mut v) })?;
    Ok(v)
}

pub(crate) fn discovery_set_spot_owner_sync_enabled(
    discovery: &Discovery,
    enabled: bool,
) -> Result<(), ConfigError> {
    let value: i32 = if enabled { 1 } else { 0 };
    check_config_rc(unsafe {
        ffi::zlink_set_option(
            discovery.raw(),
            ffi::zlink_option_t::ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC,
            (&value as *const i32).cast(),
            std::mem::size_of::<i32>(),
        )
    })
}

pub(crate) fn discovery_spot_owner_sync_enabled(
    discovery: &Discovery,
) -> Result<bool, ConfigError> {
    let mut value: i32 = 0;
    let mut len = std::mem::size_of::<i32>();
    check_config_rc(unsafe {
        ffi::zlink_get_option(
            discovery.raw(),
            ffi::zlink_option_t::ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC,
            (&mut value as *mut i32).cast(),
            &mut len,
        )
    })?;
    Ok(value != 0)
}

pub(crate) fn discovery_set_actor_route_sync_enabled(
    discovery: &Discovery,
    enabled: bool,
) -> Result<(), ConfigError> {
    let value: i32 = if enabled { 1 } else { 0 };
    check_config_rc(unsafe {
        ffi::zlink_set_option(
            discovery.raw(),
            ffi::zlink_option_t::ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
            (&value as *const i32).cast(),
            std::mem::size_of::<i32>(),
        )
    })
}

pub(crate) fn discovery_actor_route_sync_enabled(
    discovery: &Discovery,
) -> Result<bool, ConfigError> {
    let mut value: i32 = 0;
    let mut len = std::mem::size_of::<i32>();
    check_config_rc(unsafe {
        ffi::zlink_get_option(
            discovery.raw(),
            ffi::zlink_option_t::ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
            (&mut value as *mut i32).cast(),
            &mut len,
        )
    })?;
    Ok(value != 0)
}

pub(crate) fn discovery_resolve_spot(
    discovery: &Discovery,
    spot_rid: &RoutingId,
) -> Result<SpotRoute, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_spot_route_t>::zeroed();
    check_config_rc(unsafe {
        ffi::zlink_discovery_resolve_spot(discovery.raw(), spot_rid.as_raw(), raw.as_mut_ptr())
    })?;
    Ok(SpotRoute::from_raw(&unsafe { raw.assume_init() }))
}

pub(crate) fn discovery_resolve_actor(
    discovery: &Discovery,
    actor_id: &str,
) -> Result<ActorRoute, ConfigError> {
    let c_actor_id = fixed_cstring_config(actor_id, "actor_id")?;
    let mut raw = MaybeUninit::<ffi::zlink_actor_route_t>::zeroed();
    check_config_rc(unsafe {
        ffi::zlink_discovery_resolve_actor(discovery.raw(), c_actor_id.as_ptr(), raw.as_mut_ptr())
    })?;
    Ok(ActorRoute::from_raw(&unsafe { raw.assume_init() }))
}

pub(crate) fn discovery_member_peers(
    discovery: &Discovery,
) -> Result<Vec<MemberPeerEntry>, ConfigError> {
    let handle = discovery.raw();
    let count = count_entries_config(|count| unsafe {
        ffi::zlink_discovery_member_peers(handle, ptr::null_mut(), count)
    })?;
    if count == 0 {
        return Ok(Vec::new());
    }

    let mut entries = vec![unsafe { std::mem::zeroed::<ffi::zlink_member_peer_entry_t>() }; count];
    let actual = read_entries_config(
        count,
        |entries_ptr, count_ptr| unsafe {
            ffi::zlink_discovery_member_peers(handle, entries_ptr, count_ptr)
        },
        entries.as_mut_ptr(),
    )?;
    entries[..actual]
        .iter()
        .map(MemberPeerEntry::from_raw)
        .collect()
}

pub(crate) fn discovery_set_tls_client(
    discovery: &Discovery,
    ca_cert_pem: &str,
    hostname: &str,
    trust_system: bool,
) -> Result<(), ConfigError> {
    set_tls_client_config(discovery.raw(), ca_cert_pem, hostname, trust_system)
}

pub(crate) fn discovery_close(discovery: &mut Discovery) -> Result<(), CloseError> {
    destroy_handle_close(
        &mut discovery_native_mut(discovery).handle,
        ffi::zlink_discovery_destroy,
    )
}

impl Discovery {
    pub(crate) fn raw(&self) -> *mut c_void {
        discovery_native(self).handle
    }
}

impl Drop for Discovery {
    fn drop(&mut self) {
        let _ = destroy_handle(
            &mut discovery_native_mut(self).handle,
            ffi::zlink_discovery_destroy,
        );
    }
}
