use super::*;

struct NativeRegistryQueryClient {
    handle: *mut c_void,
}

unsafe impl Send for NativeRegistryQueryClient {}

impl RegistryQueryClientRuntime for NativeRegistryQueryClient {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

fn registry_query_client_native(client: &RegistryQueryClient) -> &NativeRegistryQueryClient {
    client
        .inner
        .as_any()
        .downcast_ref::<NativeRegistryQueryClient>()
        .expect("zlink native registry query client")
}

fn registry_query_client_native_mut(
    client: &mut RegistryQueryClient,
) -> &mut NativeRegistryQueryClient {
    client
        .inner
        .as_any_mut()
        .downcast_mut::<NativeRegistryQueryClient>()
        .expect("zlink native registry query client")
}

pub(crate) fn registry_query_client_new(
    ctx: &crate::core_context::Context,
) -> Result<RegistryQueryClient, ConfigError> {
    let handle = unsafe { ffi::zlink_registry_query_client_new(crate::ctx::context_handle(ctx)) };
    if handle.is_null() {
        return Err(ConfigError::new(
            crate::error::ConfigResult::InvalidHandle,
            last_errno(),
        ));
    }
    Ok(RegistryQueryClient {
        inner: Box::new(NativeRegistryQueryClient { handle }),
    })
}

pub(crate) fn registry_query_client_connect(
    client: &RegistryQueryClient,
    endpoint: &str,
) -> Result<(), ConnectError> {
    let c = CString::new(endpoint).map_err(|_| {
        ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
    })?;
    let handle = registry_query_client_native(client).handle;
    check_connect_rc(unsafe { ffi::zlink_registry_query_client_connect(handle, c.as_ptr()) })
}

pub(crate) fn registry_query_client_topology(
    client: &RegistryQueryClient,
    filter: Option<&RegistryTopologyFilter>,
) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
    registry_query_client_snapshot_query_opt(client, filter)
}

pub(crate) fn registry_query_client_snapshot(
    client: &RegistryQueryClient,
    filter: Option<&RegistryTopologyFilter>,
) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
    registry_query_client_snapshot_query_opt(client, filter)
}

pub(crate) fn registry_query_client_close(
    client: &mut RegistryQueryClient,
) -> Result<(), CloseError> {
    destroy_handle_close(
        &mut registry_query_client_native_mut(client).handle,
        ffi::zlink_registry_query_client_destroy,
    )
}

impl Drop for RegistryQueryClient {
    fn drop(&mut self) {
        let _ = destroy_handle(
            &mut registry_query_client_native_mut(self).handle,
            ffi::zlink_registry_query_client_destroy,
        );
    }
}

pub(crate) fn registry_status(registry: &Registry) -> Result<RegistryStatus, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_registry_status_t>::uninit();
    check_config_rc(unsafe { ffi::zlink_registry_status(registry.raw(), raw.as_mut_ptr()) })?;
    let raw = unsafe { raw.assume_init() };
    Ok(RegistryStatus::from_raw(&raw))
}

pub(crate) fn registry_service_summary(
    registry: &Registry,
) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
    registry_service_summary_query_opt(registry, None)
}

pub(crate) fn registry_service_summary_query(
    registry: &Registry,
    filter: &RegistryServiceSummaryFilter,
) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
    registry_service_summary_query_opt(registry, Some(filter))
}

pub(crate) fn registry_member_peers(
    registry: &Registry,
    channel_name: &str,
) -> Result<Vec<MemberPeerEntry>, ConfigError> {
    let c_channel_name = fixed_cstring_config(channel_name, "channel_name")?;
    let handle = registry.raw();
    let count = count_entries_config(|count| unsafe {
        ffi::zlink_registry_member_peers(handle, c_channel_name.as_ptr(), ptr::null_mut(), count)
    })?;
    if count == 0 {
        return Ok(Vec::new());
    }

    let mut entries = vec![unsafe { std::mem::zeroed::<ffi::zlink_member_peer_entry_t>() }; count];
    let actual = read_entries_config(
        count,
        |entries_ptr, count_ptr| unsafe {
            ffi::zlink_registry_member_peers(
                handle,
                c_channel_name.as_ptr(),
                entries_ptr,
                count_ptr,
            )
        },
        entries.as_mut_ptr(),
    )?;
    entries[..actual]
        .iter()
        .map(MemberPeerEntry::from_raw)
        .collect()
}

pub(crate) fn registry_topology(
    registry: &Registry,
) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
    registry_topology_opt(registry, None)
}

pub(crate) fn registry_topology_query(
    registry: &Registry,
    filter: &RegistryTopologyFilter,
) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
    registry_topology_opt(registry, Some(filter))
}

impl SpotNode {
    pub(super) fn subjects_query_opt(
        &self,
        filter: Option<&SpotNodeSubjectFilter>,
    ) -> Result<Vec<SpotNodeSubjectEntry>, ConfigError> {
        let read = |filter_ptr: *const ffi::zlink_spot_node_subject_filter_t| {
            let count = count_entries_config(|count| unsafe {
                ffi::zlink_spot_node_subjects(
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
                vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_subject_entry_t>() }; count];
            let actual = read_entries_config(
                count,
                |entries_ptr, count_ptr| unsafe {
                    ffi::zlink_spot_node_subjects(
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
                .map(SpotNodeSubjectEntry::from_raw)
                .collect())
        };

        match filter {
            Some(filter) => with_spot_node_subject_filter_config(filter, read),
            None => read(ptr::null()),
        }
    }

    pub(super) fn internal_sockets_opt(
        &self,
        filter: Option<&SpotNodeSocketFilter>,
    ) -> Result<Vec<SpotNodeSocketEntry>, ConfigError> {
        let read = |filter_ptr: *const ffi::zlink_spot_node_socket_filter_t| {
            let count = count_entries_config(|count| unsafe {
                ffi::zlink_spot_node_internal_sockets(
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
                vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_socket_entry_t>() }; count];
            let actual = read_entries_config(
                count,
                |entries_ptr, count_ptr| unsafe {
                    ffi::zlink_spot_node_internal_sockets(
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
                .map(SpotNodeSocketEntry::from_raw)
                .collect())
        };

        match filter {
            Some(filter) => with_spot_node_socket_filter_config(filter, read),
            None => read(ptr::null()),
        }
    }
}

fn registry_service_summary_query_opt(
    registry: &Registry,
    filter: Option<&RegistryServiceSummaryFilter>,
) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
    let handle = registry.raw();
    let read = |filter_ptr: *const ffi::zlink_registry_service_summary_filter_t| {
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_registry_service_summary(handle, filter_ptr, ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }

        let mut entries =
            vec![
                unsafe { std::mem::zeroed::<ffi::zlink_registry_service_summary_entry_t>() };
                count
            ];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_registry_service_summary(handle, filter_ptr, entries_ptr, count_ptr)
            },
            entries.as_mut_ptr(),
        )?;
        Ok(entries[..actual]
            .iter()
            .map(RegistryServiceSummaryEntry::from_raw)
            .collect())
    };

    match filter {
        Some(filter) => with_registry_service_summary_filter_config(filter, read),
        None => read(ptr::null()),
    }
}

fn registry_topology_opt(
    registry: &Registry,
    filter: Option<&RegistryTopologyFilter>,
) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
    let handle = registry.raw();
    let read = |filter_ptr: *const ffi::zlink_registry_topology_filter_t| {
        let count = count_entries_config(|count| unsafe {
            if filter_ptr.is_null() {
                ffi::zlink_registry_topology(handle, ptr::null(), ptr::null_mut(), count)
            } else {
                ffi::zlink_registry_topology(handle, filter_ptr, ptr::null_mut(), count)
            }
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }

        let mut entries =
            vec![MaybeUninit::<ffi::zlink_registry_topology_entry_t>::uninit(); count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                if filter_ptr.is_null() {
                    ffi::zlink_registry_topology(handle, ptr::null(), entries_ptr.cast(), count_ptr)
                } else {
                    ffi::zlink_registry_topology(handle, filter_ptr, entries_ptr.cast(), count_ptr)
                }
            },
            entries.as_mut_ptr(),
        )?;
        Ok(entries[..actual]
            .iter()
            .map(|entry| RegistryTopologyEntry::from_raw(unsafe { entry.assume_init_ref() }))
            .collect())
    };

    match filter {
        Some(filter) => with_registry_topology_filter_config(filter, read),
        None => read(ptr::null()),
    }
}

fn registry_query_client_snapshot_query_opt(
    client: &RegistryQueryClient,
    filter: Option<&RegistryTopologyFilter>,
) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
    let handle = registry_query_client_native(client).handle;
    let read = |filter_ptr| {
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_registry_query_client_topology(handle, filter_ptr, ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }

        let mut entries =
            vec![MaybeUninit::<ffi::zlink_registry_topology_entry_t>::uninit(); count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_registry_query_client_topology(
                    handle,
                    filter_ptr,
                    entries_ptr.cast(),
                    count_ptr,
                )
            },
            entries.as_mut_ptr(),
        )?;
        Ok(entries[..actual]
            .iter()
            .map(|entry| RegistryTopologyEntry::from_raw(unsafe { entry.assume_init_ref() }))
            .collect())
    };

    match filter {
        Some(filter) => with_registry_topology_filter_config(filter, read),
        None => read(ptr::null()),
    }
}

impl Drop for Spot {
    fn drop(&mut self) {
        let _ = destroy_handle(&mut spot_native_mut(self).handle, ffi::zlink_spot_destroy);
    }
}
