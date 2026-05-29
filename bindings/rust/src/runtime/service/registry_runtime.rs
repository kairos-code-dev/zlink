// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

struct NativeRegistry {
    handle: *mut c_void,
}

unsafe impl Send for NativeRegistry {}

impl RegistryRuntime for NativeRegistry {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

fn registry_native(registry: &Registry) -> &NativeRegistry {
    registry
        .inner
        .as_any()
        .downcast_ref::<NativeRegistry>()
        .expect("zlink native registry")
}

fn registry_native_mut(registry: &mut Registry) -> &mut NativeRegistry {
    registry
        .inner
        .as_any_mut()
        .downcast_mut::<NativeRegistry>()
        .expect("zlink native registry")
}

impl Registry {
    pub fn new(ctx: &crate::core_context::Context) -> Result<Self, ConfigError> {
        let handle = unsafe { ffi::zlink_registry_new(crate::ctx::context_handle(ctx)) };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self {
            inner: Box::new(NativeRegistry { handle }),
        })
    }

    pub fn bind(&self, pub_endpoint: &str, router_endpoint: &str) -> Result<(), BindError> {
        let c_pub = CString::new(pub_endpoint)
            .map_err(|_| BindError::new(crate::error::BindResult::InvalidArgument, libc::EINVAL))?;
        let c_router = CString::new(router_endpoint)
            .map_err(|_| BindError::new(crate::error::BindResult::InvalidArgument, libc::EINVAL))?;
        let handle = self.raw();
        check_bind_rc(unsafe {
            ffi::zlink_registry_bind(handle, c_pub.as_ptr(), c_router.as_ptr())
        })
    }

    pub fn set_id(&self, id: u32) -> Result<(), ConfigError> {
        self.set_option(ffi::ZLINK_REGISTRY_OPT_ID, id)
    }

    fn set_option(
        &self,
        option: ffi::zlink_registry_option_t,
        value: u32,
    ) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_registry_set(self.raw(), option, value) })
    }

    pub fn add_peer(&self, peer_pub_endpoint: &str) -> Result<(), ConnectError> {
        let c = CString::new(peer_pub_endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe { ffi::zlink_registry_add_peer(self.raw(), c.as_ptr()) })
    }

    pub fn set_heartbeat(&self, interval_ms: u32, timeout_ms: u32) -> Result<(), ConfigError> {
        self.set_option(ffi::ZLINK_REGISTRY_OPT_HEARTBEAT_INTERVAL_MS, interval_ms)?;
        self.set_option(ffi::ZLINK_REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS, timeout_ms)
    }

    pub fn set_broadcast_interval(&self, interval_ms: u32) -> Result<(), ConfigError> {
        self.set_option(ffi::ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS, interval_ms)
    }

    pub fn set_tls_server(
        &self,
        cert_pem: &str,
        key_pem: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        set_tls_server_config(self.raw(), cert_pem, key_pem, require_client_cert)
    }

    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        set_tls_client_config(self.raw(), ca_cert_pem, hostname, trust_system)
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        destroy_handle_close(
            &mut registry_native_mut(self).handle,
            ffi::zlink_registry_destroy,
        )
    }

    pub(crate) fn raw(&self) -> *mut c_void {
        registry_native(self).handle
    }
}

impl Drop for Registry {
    fn drop(&mut self) {
        let _ = destroy_handle(
            &mut registry_native_mut(self).handle,
            ffi::zlink_registry_destroy,
        );
    }
}
