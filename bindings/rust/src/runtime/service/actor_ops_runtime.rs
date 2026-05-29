use super::*;

// ---------------------------------------------------------------------------
// Actor value structs
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Actor operation builders
// ---------------------------------------------------------------------------

/// Async Actor join builder. Payload accumulates via `.message(...)`.
pub struct ActorJoinOp<State> {
    pub(super) node_handle: *mut c_void,
    pub(super) spot_handle: *mut c_void,
    pub(super) actor: ffi::zlink_actor_ref_t,
    pub(super) dest_node_rid: RoutingId,
    pub(super) dest_spot_rid: RoutingId,
    pub(super) parts: Vec<Message>,
    pub(super) flags: SendFlags,
    pub(super) timeout: Duration,
    pub(super) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorJoinOp<S> {}

/// Async Actor Entry Spot join builder.
pub struct ActorJoinEntrySpotOp<State> {
    pub(super) node_handle: *mut c_void,
    pub(super) actor: ffi::zlink_actor_ref_t,
    pub(super) dest_node_rid: RoutingId,
    pub(super) timeout: Duration,
    pub(super) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorJoinEntrySpotOp<S> {}

impl ActorJoinEntrySpotOp<Empty> {
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.timeout = timeout;
        self
    }

    /// Submit and await completion (async).
    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<ActorJoinEntrySpotResult, ZlinkError> {
        let (tx, rx) = mpsc::channel();
        self.submit(move |result| {
            let _ = tx.send(result);
        })
        .map_err(ZlinkError::from)?;
        rx.recv().map_err(|_| {
            ZlinkError::from(RequestError::new(
                crate::error::RequestResult::ProtocolError,
                libc::EINVAL,
            ))
        })
    }

    /// Submit with a completion callback.
    /// # Errors: SubmitError
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinEntrySpotResult) + Send + 'static,
    {
        let state_ptr = Box::into_raw(Box::new(ActorJoinEntrySpotCallbackState {
            callback: Some(Box::new(callback)),
        }));
        let timeout_ms = timeout_to_timeout_ms(self.timeout);
        let rc = unsafe {
            ffi::zlink_spot_node_actor_join_entry_spot(
                self.node_handle,
                &self.actor,
                self.dest_node_rid.as_raw(),
                Some(actor_join_entry_spot_user_callback),
                state_ptr.cast(),
                timeout_ms,
            )
        };
        if rc != 0 {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
        }
        check_submit_rc(rc)
    }
}

impl ActorJoinOp<Empty> {
    pub fn message(self, message: Message) -> ActorJoinOp<Ready> {
        ActorJoinOp {
            node_handle: self.node_handle,
            spot_handle: self.spot_handle,
            actor: self.actor,
            dest_node_rid: self.dest_node_rid,
            dest_spot_rid: self.dest_spot_rid,
            parts: vec![message],
            flags: self.flags,
            timeout: self.timeout,
            _state: std::marker::PhantomData,
        }
    }
}

impl ActorJoinOp<Ready> {
    pub fn message(mut self, message: Message) -> Self {
        self.parts.push(message);
        self
    }

    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.timeout = timeout;
        self
    }

    pub fn flags(mut self, flags: SendFlags) -> Self {
        self.flags = flags;
        self
    }

    /// Submit and await completion (async). Returns `(ActorJoinResult, parts)`.
    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<(ActorJoinResult, Vec<Message>), ZlinkError> {
        let (tx, rx) = mpsc::channel();
        self.submit(move |result, parts| {
            let _ = tx.send((result, parts));
        })
        .map_err(ZlinkError::from)?;
        rx.recv().map_err(|_| {
            ZlinkError::from(RequestError::new(
                crate::error::RequestResult::ProtocolError,
                libc::EINVAL,
            ))
        })
    }

    /// Submit with a completion callback.
    /// # Errors: SubmitError
    pub fn submit<F>(mut self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinResult, Vec<Message>) + Send + 'static,
    {
        let native = prepare_send_parts(&mut self.parts)?;
        let mut native = native;
        let state_ptr = Box::into_raw(Box::new(ActorJoinCallbackState {
            callback: Some(Box::new(callback)),
            _progress: if self.spot_handle.is_null() {
                None
            } else {
                Some(RequestProgressGuard::attach_spot(self.spot_handle))
            },
        }));
        let timeout_ms = timeout_to_timeout_ms(self.timeout);
        let flags_bits = self.flags.bits();
        let rc = unsafe {
            ffi::zlink_spot_node_actor_join_spot(
                self.node_handle,
                &self.actor,
                self.dest_node_rid.as_raw(),
                self.dest_spot_rid.as_raw(),
                native.as_mut_ptr(),
                native.len(),
                Some(actor_join_user_callback),
                state_ptr.cast(),
                flags_bits,
                timeout_ms,
            )
        };
        if rc != 0 {
            for part in native.iter_mut() {
                unsafe {
                    ffi::zlink_msg_close(part);
                }
            }
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
        }
        check_submit_rc(rc)
    }
}

/// Builder for replying to an Actor join admission request. 0-part submit is allowed.
pub struct ActorJoinReplyOp<State> {
    pub(super) spot_handle: *mut c_void,
    pub(super) info: ffi::zlink_actor_join_info_t,
    pub(super) join_result_code: i32,
    pub(super) parts: Vec<Message>,
    pub(super) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorJoinReplyOp<S> {}

impl ActorJoinReplyOp<Empty> {
    pub fn message(mut self, message: Message) -> ActorJoinReplyOp<Empty> {
        self.parts.push(message);
        self
    }

    /// # Errors: SubmitError
    pub fn submit(mut self) -> Result<(), SubmitError> {
        // 0..N parts allowed.
        let mut native: Vec<ffi::zlink_msg_t> = Vec::with_capacity(self.parts.len());
        unsafe {
            for part in self.parts.iter_mut() {
                let mut dest = MaybeUninit::<ffi::zlink_msg_t>::uninit();
                ffi::zlink_msg_init(dest.as_mut_ptr());
                ffi::zlink_msg_move(dest.as_mut_ptr(), part.raw_mut());
                native.push(dest.assume_init());
            }
        }
        let rc = unsafe {
            ffi::zlink_spot_actor_join_reply(
                self.spot_handle,
                &self.info,
                self.join_result_code,
                if native.is_empty() {
                    std::ptr::null_mut()
                } else {
                    native.as_mut_ptr()
                },
                native.len(),
            )
        };
        if rc != 0 {
            for part in native.iter_mut() {
                unsafe {
                    ffi::zlink_msg_close(part);
                }
            }
        }
        check_submit_rc(rc)
    }
}

/// Payload-less builder shared by leave / destroy / bind / unbind.
pub(super) struct ActorReplyOpInner {
    pub(super) handle: *mut c_void,
    pub(super) kind: ActorReplyOpKind,
    pub(super) timeout: Duration,
}

pub(super) enum ActorReplyOpKind {
    Leave {
        actor: ffi::zlink_actor_ref_t,
        current_spot_rid: RoutingId,
    },
    Destroy {
        actor: ffi::zlink_actor_ref_t,
    },
    Bind {
        session_rid: RoutingId,
        actor: ffi::zlink_actor_ref_t,
    },
    Unbind {
        session_rid: RoutingId,
        actor_id: std::ffi::CString,
    },
}

impl ActorReplyOpInner {
    fn submit_callback<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        let state_ptr = Box::into_raw(Box::new(SpotReplyCallbackState {
            callback: Some(Box::new(callback)),
            _progress: None,
        }));
        let timeout_ms = timeout_to_timeout_ms(self.timeout);
        let rc = match &self.kind {
            ActorReplyOpKind::Leave {
                actor,
                current_spot_rid,
            } => unsafe {
                ffi::zlink_spot_node_actor_leave_spot(
                    self.handle,
                    actor,
                    current_spot_rid.as_raw(),
                    spot_reply_callback,
                    state_ptr.cast(),
                    timeout_ms,
                )
            },
            ActorReplyOpKind::Destroy { actor } => unsafe {
                ffi::zlink_spot_node_actor_destroy(
                    self.handle,
                    actor,
                    spot_reply_callback,
                    state_ptr.cast(),
                    timeout_ms,
                )
            },
            ActorReplyOpKind::Bind { session_rid, actor } => unsafe {
                ffi::zlink_stream_bind_actor(
                    self.handle,
                    session_rid.as_raw(),
                    actor,
                    spot_reply_callback,
                    state_ptr.cast(),
                    timeout_ms,
                )
            },
            ActorReplyOpKind::Unbind {
                session_rid,
                actor_id,
            } => unsafe {
                ffi::zlink_stream_unbind_actor(
                    self.handle,
                    session_rid.as_raw(),
                    actor_id.as_ptr(),
                    spot_reply_callback,
                    state_ptr.cast(),
                    timeout_ms,
                )
            },
        };
        if rc != 0 {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
        }
        check_submit_rc(rc)
    }
}

/// Async Actor leave builder (payload-less).
pub struct ActorLeaveOp<State> {
    pub(super) inner: ActorReplyOpInner,
    pub(super) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorLeaveOp<S> {}

impl ActorLeaveOp<Empty> {
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.inner.timeout = timeout;
        self
    }

    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
        actor_reply_op_submit_async(self.inner).await
    }

    /// # Errors: SubmitError
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        self.inner.submit_callback(callback)
    }
}

/// Async Actor destroy builder (payload-less).
pub struct ActorDestroyOp<State> {
    pub(super) inner: ActorReplyOpInner,
    pub(super) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorDestroyOp<S> {}

impl ActorDestroyOp<Empty> {
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.inner.timeout = timeout;
        self
    }

    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
        actor_reply_op_submit_async(self.inner).await
    }

    /// # Errors: SubmitError
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        self.inner.submit_callback(callback)
    }
}

/// Async Actor bind builder (payload-less).
pub struct ActorBindOp<State> {
    pub(super) inner: ActorReplyOpInner,
    pub(super) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorBindOp<S> {}

impl ActorBindOp<Empty> {
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.inner.timeout = timeout;
        self
    }

    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
        actor_reply_op_submit_async(self.inner).await
    }

    /// # Errors: SubmitError
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        self.inner.submit_callback(callback)
    }
}

/// Async Actor unbind builder (payload-less).
pub struct ActorUnbindOp<State> {
    pub(super) inner: ActorReplyOpInner,
    pub(super) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorUnbindOp<S> {}

impl ActorUnbindOp<Empty> {
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.inner.timeout = timeout;
        self
    }

    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
        actor_reply_op_submit_async(self.inner).await
    }

    /// # Errors: SubmitError
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        self.inner.submit_callback(callback)
    }
}

async fn actor_reply_op_submit_async(inner: ActorReplyOpInner) -> Result<Vec<Message>, ZlinkError> {
    let (tx, rx) = mpsc::channel();
    inner
        .submit_callback(move |result| {
            let _ = tx.send(result);
        })
        .map_err(ZlinkError::from)?;
    rx.recv()
        .unwrap_or_else(|_| {
            Err(RequestError::new(
                crate::error::RequestResult::ProtocolError,
                libc::EINVAL,
            ))
        })
        .map_err(ZlinkError::from)
}

/// Async remote Actor lookup builder (payload-less).
pub struct ActorLookupOp<State> {
    pub(super) node_handle: *mut c_void,
    pub(super) target_node_rid: RoutingId,
    pub(super) actor_id: std::ffi::CString,
    pub(super) timeout: Duration,
    pub(super) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorLookupOp<S> {}

impl ActorLookupOp<Empty> {
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.timeout = timeout;
        self
    }

    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<ActorLookupResult, ZlinkError> {
        let (tx, rx) = mpsc::channel();
        self.submit(move |result| {
            let _ = tx.send(result);
        })
        .map_err(ZlinkError::from)?;
        rx.recv().map_err(|_| {
            ZlinkError::from(RequestError::new(
                crate::error::RequestResult::ProtocolError,
                libc::EINVAL,
            ))
        })
    }

    /// # Errors: SubmitError
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorLookupResult) + Send + 'static,
    {
        let state_ptr = Box::into_raw(Box::new(ActorLookupCallbackState {
            callback: Some(Box::new(callback)),
        }));
        let rc = unsafe {
            ffi::zlink_remote_actor_get_ref(
                self.node_handle,
                self.target_node_rid.as_raw(),
                self.actor_id.as_ptr(),
                actor_lookup_user_callback,
                state_ptr.cast(),
                timeout_to_timeout_ms(self.timeout),
            )
        };
        if rc != 0 {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
        }
        check_submit_rc(rc)
    }
}

type ActorJoinCallback = Box<dyn FnOnce(ActorJoinResult, Vec<Message>) + Send>;

struct ActorJoinCallbackState {
    callback: Option<ActorJoinCallback>,
    _progress: Option<RequestProgressGuard>,
}

struct ActorJoinEntrySpotCallbackState {
    callback: Option<Box<dyn FnOnce(ActorJoinEntrySpotResult) + Send>>,
}

struct ActorLookupCallbackState {
    callback: Option<Box<dyn FnOnce(ActorLookupResult) + Send>>,
}

unsafe extern "C" fn actor_join_user_callback(
    result: *const ffi::zlink_actor_join_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let mut state = unsafe { Box::from_raw(userdata.cast::<ActorJoinCallbackState>()) };
    let callback = state.callback.take().expect("actor join callback");
    if result.is_null() {
        let placeholder = ActorJoinResult {
            result: crate::error::RequestResult::InternalError,
            join_result_code: 0,
            actor: ActorRef {
                node_rid: RoutingId::from_raw(ffi::zlink_routing_id_t {
                    size: 0,
                    data: [0; 255],
                }),
                actor_id: String::new(),
                generation: 0,
            },
            joined_spot_rid: RoutingId::from_raw(ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            }),
            join_epoch: 0,
            flags: 0,
        };
        callback(placeholder, Vec::new());
        return;
    }
    let raw = unsafe { *result };
    let join_result = ActorJoinResult {
        result: request_result_from_raw(raw.result),
        join_result_code: raw.join_result_code,
        actor: ActorRef::from_raw(&raw.actor),
        joined_spot_rid: RoutingId::from_raw(raw.joined_spot_rid),
        join_epoch: raw.join_epoch,
        flags: raw.flags,
    };
    let messages = borrowed_parts_to_messages(parts, part_count);
    callback(join_result, messages);
}

unsafe extern "C" fn actor_join_entry_spot_user_callback(
    result: *const ffi::zlink_actor_join_entry_spot_result_t,
    userdata: *mut c_void,
) {
    let mut state = unsafe { Box::from_raw(userdata.cast::<ActorJoinEntrySpotCallbackState>()) };
    let callback = state
        .callback
        .take()
        .expect("actor entry spot join callback");
    if result.is_null() {
        let placeholder = ActorJoinEntrySpotResult {
            result: crate::error::RequestResult::InternalError,
            actor: ActorRef {
                node_rid: RoutingId::from_raw(ffi::zlink_routing_id_t {
                    size: 0,
                    data: [0; 255],
                }),
                actor_id: String::new(),
                generation: 0,
            },
            target_node_rid: RoutingId::from_raw(ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            }),
            join_epoch: 0,
            flags: 0,
        };
        callback(placeholder);
        return;
    }
    let raw = unsafe { *result };
    callback(ActorJoinEntrySpotResult {
        result: request_result_from_raw(raw.result),
        actor: ActorRef::from_raw(&raw.actor),
        target_node_rid: RoutingId::from_raw(raw.target_node_rid),
        join_epoch: raw.join_epoch,
        flags: raw.flags,
    });
}

unsafe extern "C" fn actor_lookup_user_callback(
    result: *const ffi::zlink_actor_lookup_result_t,
    userdata: *mut c_void,
) {
    let mut state = unsafe { Box::from_raw(userdata.cast::<ActorLookupCallbackState>()) };
    let callback = state.callback.take().expect("actor lookup callback");
    if result.is_null() {
        let placeholder = ActorLookupResult {
            result: crate::error::RequestResult::InternalError,
            actor: ActorRef {
                node_rid: RoutingId::from_raw(ffi::zlink_routing_id_t {
                    size: 0,
                    data: [0; 255],
                }),
                actor_id: String::new(),
                generation: 0,
            },
            flags: 0,
        };
        callback(placeholder);
        return;
    }
    let raw = unsafe { *result };
    let lookup_result = ActorLookupResult {
        result: request_result_from_raw(raw.result),
        actor: ActorRef::from_raw(&raw.actor),
        flags: raw.flags,
    };
    callback(lookup_result);
}
