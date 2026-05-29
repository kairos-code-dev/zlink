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

impl RegistryQueryClient {
    pub fn new(ctx: &crate::core_context::Context) -> Result<Self, ConfigError> {
        let handle =
            unsafe { ffi::zlink_registry_query_client_new(crate::ctx::context_handle(ctx)) };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self {
            inner: Box::new(NativeRegistryQueryClient { handle }),
        })
    }

    pub fn connect(&self, endpoint: &str) -> Result<(), ConnectError> {
        let c = CString::new(endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        let handle = registry_query_client_native(self).handle;
        check_connect_rc(unsafe { ffi::zlink_registry_query_client_connect(handle, c.as_ptr()) })
    }

    pub fn topology(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        self.snapshot_query_opt(filter)
    }

    pub fn snapshot(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        self.snapshot_query_opt(filter)
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        destroy_handle_close(
            &mut registry_query_client_native_mut(self).handle,
            ffi::zlink_registry_query_client_destroy,
        )
    }
}

impl Drop for RegistryQueryClient {
    fn drop(&mut self) {
        let _ = destroy_handle(
            &mut registry_query_client_native_mut(self).handle,
            ffi::zlink_registry_query_client_destroy,
        );
    }
}

impl Registry {
    pub fn status(&self) -> Result<RegistryStatus, ConfigError> {
        let mut raw = MaybeUninit::<ffi::zlink_registry_status_t>::uninit();
        check_config_rc(unsafe { ffi::zlink_registry_status(self.raw(), raw.as_mut_ptr()) })?;
        let raw = unsafe { raw.assume_init() };
        Ok(RegistryStatus::from_raw(&raw))
    }

    pub fn service_summary(&self) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
        self.service_summary_query_opt(None)
    }

    pub fn service_summary_query(
        &self,
        filter: &RegistryServiceSummaryFilter,
    ) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
        self.service_summary_query_opt(Some(filter))
    }

    pub fn member_peers(&self, channel_name: &str) -> Result<Vec<MemberPeerEntry>, ConfigError> {
        let c_channel_name = fixed_cstring_config(channel_name, "channel_name")?;
        let handle = self.raw();
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_registry_member_peers(
                handle,
                c_channel_name.as_ptr(),
                ptr::null_mut(),
                count,
            )
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }

        let mut entries =
            vec![unsafe { std::mem::zeroed::<ffi::zlink_member_peer_entry_t>() }; count];
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

    pub fn topology(&self) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        self.topology_opt(None)
    }

    pub fn topology_query(
        &self,
        filter: &RegistryTopologyFilter,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        self.topology_opt(Some(filter))
    }
}

impl SpotNode {
    fn subjects_query_opt(
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

    fn internal_sockets_opt(
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

impl Registry {
    fn service_summary_query_opt(
        &self,
        filter: Option<&RegistryServiceSummaryFilter>,
    ) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
        let handle = self.raw();
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

    fn topology_opt(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        let handle = self.raw();
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
                        ffi::zlink_registry_topology(
                            handle,
                            ptr::null(),
                            entries_ptr.cast(),
                            count_ptr,
                        )
                    } else {
                        ffi::zlink_registry_topology(
                            handle,
                            filter_ptr,
                            entries_ptr.cast(),
                            count_ptr,
                        )
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
}

impl RegistryQueryClient {
    fn snapshot_query_opt(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        let handle = registry_query_client_native(self).handle;
        let read = |filter_ptr| {
            let count = count_entries_config(|count| unsafe {
                ffi::zlink_registry_query_client_topology(
                    handle,
                    filter_ptr,
                    ptr::null_mut(),
                    count,
                )
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
}

impl Drop for Spot {
    fn drop(&mut self) {
        let _ = destroy_handle(&mut spot_native_mut(self).handle, ffi::zlink_spot_destroy);
    }
}
