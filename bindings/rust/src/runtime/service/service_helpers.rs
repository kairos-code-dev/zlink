use super::*;

pub(super) fn fixed_cstr_to_string(buf: &[c_char]) -> String {
    unsafe {
        let ptr = buf.as_ptr();
        if *ptr == 0 {
            String::new()
        } else {
            CStr::from_ptr(ptr).to_string_lossy().into_owned()
        }
    }
}

pub(super) fn fixed_cstring_config(value: &str, _label: &str) -> Result<CString, ConfigError> {
    if value.len() > 255 {
        return Err(config_validation_error());
    }
    CString::new(value).map_err(|_| config_validation_error())
}

pub(crate) fn fixed_cstring_or_panic(value: &str, label: &str) -> CString {
    fixed_cstring_config(value, label).unwrap_or_else(|_| panic!("invalid {label}"))
}

pub(super) fn set_tls_server_config(
    handle: *mut c_void,
    cert_pem: &str,
    key_pem: &str,
    require_client_cert: bool,
) -> Result<(), ConfigError> {
    let cert = fixed_cstring_config(cert_pem, "cert_pem")?;
    let key = fixed_cstring_config(key_pem, "key_pem")?;
    check_config_rc(unsafe {
        ffi::zlink_set_tls_server(
            handle,
            cert.as_ptr(),
            key.as_ptr(),
            if require_client_cert { 1 } else { 0 },
        )
    })
}

pub(super) fn set_tls_client_config(
    handle: *mut c_void,
    ca_cert_pem: &str,
    hostname: &str,
    trust_system: bool,
) -> Result<(), ConfigError> {
    let ca = fixed_cstring_config(ca_cert_pem, "ca_cert_pem")?;
    let host = fixed_cstring_config(hostname, "hostname")?;
    check_config_rc(unsafe {
        ffi::zlink_set_tls_client(
            handle,
            ca.as_ptr(),
            host.as_ptr(),
            if trust_system { 1 } else { 0 },
        )
    })
}

pub(super) fn destroy_handle(
    handle: &mut *mut c_void,
    destroy: unsafe extern "C" fn(*mut *mut c_void) -> i32,
) -> Result<(), ZlinkError> {
    if handle.is_null() {
        return Ok(());
    }
    let mut h = *handle;
    check_rc(unsafe { destroy(&mut h) })?;
    *handle = ptr::null_mut();
    Ok(())
}

pub(super) fn destroy_handle_close(
    handle: &mut *mut c_void,
    destroy: unsafe extern "C" fn(*mut *mut c_void) -> i32,
) -> Result<(), CloseError> {
    if handle.is_null() {
        return Ok(());
    }
    let mut h = *handle;
    check_close_rc(unsafe { destroy(&mut h) })?;
    *handle = ptr::null_mut();
    Ok(())
}

pub(super) fn write_c_array_config(
    buf: &mut [c_char],
    value: &str,
    _label: &str,
) -> Result<(), ConfigError> {
    if value.len() >= buf.len() {
        return Err(config_validation_error());
    }
    for b in buf.iter_mut() {
        *b = 0;
    }
    for (idx, byte) in value.as_bytes().iter().enumerate() {
        if *byte == 0 {
            return Err(config_validation_error());
        }
        buf[idx] = *byte as c_char;
    }
    Ok(())
}

pub(super) fn count_entries_config(
    f: impl FnOnce(*mut usize) -> i32,
) -> Result<usize, ConfigError> {
    let mut count: usize = 0;
    check_config_rc(f(&mut count))?;
    Ok(count)
}

pub(super) fn read_entries_config<T>(
    capacity: usize,
    f: impl FnOnce(*mut T, *mut usize) -> i32,
    entries_ptr: *mut T,
) -> Result<usize, ConfigError> {
    let mut count = capacity;
    check_config_rc(f(entries_ptr, &mut count))?;
    Ok(std::cmp::min(capacity, count))
}

pub(super) fn with_spot_node_peer_filter_config<T>(
    filter: &SpotNodePeerFilter,
    f: impl FnOnce(*const ffi::zlink_spot_node_peer_filter_t) -> Result<T, ConfigError>,
) -> Result<T, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_spot_node_peer_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        if let Some(peer_endpoint) = &filter.peer_endpoint {
            write_c_array_config(&mut (*ptr).peer_endpoint, peer_endpoint, "peer_endpoint")?;
        }
        if let Some(source) = filter.source {
            (*ptr).source = source.to_raw();
        }
        if let Some(state) = filter.state {
            (*ptr).state = state.to_raw();
        }
    }
    f(ptr)
}

pub(super) fn with_spot_node_subject_filter_config<T>(
    filter: &SpotNodeSubjectFilter,
    f: impl FnOnce(*const ffi::zlink_spot_node_subject_filter_t) -> Result<T, ConfigError>,
) -> Result<T, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_spot_node_subject_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        if let Some(role) = filter.role {
            (*ptr).role = role.to_raw();
        }
        if let Some(subject) = &filter.subject {
            write_c_array_config(&mut (*ptr).subject, subject, "subject")?;
        }
        if let Some(subject_kind) = filter.subject_kind {
            (*ptr).subject_kind = subject_kind as u32;
        }
    }
    f(ptr)
}

pub(super) fn with_spot_node_socket_filter_config<T>(
    filter: &SpotNodeSocketFilter,
    f: impl FnOnce(*const ffi::zlink_spot_node_socket_filter_t) -> Result<T, ConfigError>,
) -> Result<T, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_spot_node_socket_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        (*ptr).owner = filter.owner.unwrap_or(SpotNodeSocketOwner::Any).to_raw();
        if let Some(socket_type) = filter.socket_type {
            (*ptr).socket_type = socket_type.to_raw();
        }
        if let Some(socket_name) = &filter.socket_name {
            write_c_array_config(&mut (*ptr).socket_name, socket_name, "socket_name")?;
        }
    }
    f(ptr)
}

pub(super) fn with_registry_service_summary_filter_config<T>(
    filter: &RegistryServiceSummaryFilter,
    f: impl FnOnce(*const ffi::zlink_registry_service_summary_filter_t) -> Result<T, ConfigError>,
) -> Result<T, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_registry_service_summary_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        if let Some(auto_connect_type) = filter.auto_connect_type {
            (*ptr).auto_connect_type = auto_connect_type.to_raw();
        }
        if let Some(service_role) = filter.service_role {
            (*ptr).service_role = service_role.to_raw();
        }
        if let Some(channel_name) = &filter.channel_name {
            write_c_array_config(&mut (*ptr).channel_name, channel_name, "channel_name")?;
        }
    }
    f(ptr)
}

pub(super) fn with_registry_topology_filter_config<T>(
    filter: &RegistryTopologyFilter,
    f: impl FnOnce(*const ffi::zlink_registry_topology_filter_t) -> Result<T, ConfigError>,
) -> Result<T, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_registry_topology_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        if let Some(auto_connect_type) = filter.auto_connect_type {
            (*ptr).auto_connect_type = auto_connect_type.to_raw();
        }
        if let Some(service_kind) = filter.service_kind {
            (*ptr).service_kind = service_kind.to_raw();
        }
        if let Some(service_role) = filter.service_role {
            (*ptr).service_role = service_role.to_raw();
        }
        if let Some(channel_name) = &filter.channel_name {
            write_c_array_config(&mut (*ptr).channel_name, channel_name, "channel_name")?;
        }
        if let Some(routing_id) = &filter.routing_id {
            (*ptr).routing_id = *routing_id.as_raw();
        }
        if let Some(state) = filter.state {
            (*ptr).state = state.to_raw();
        }
        if let Some(source) = filter.source {
            (*ptr).source = source.to_raw();
        }
    }
    f(ptr)
}
