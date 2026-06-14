use super::*;
use crate::runtime_bridge::{
    ActorJoinEntrySpotOpInnerRuntime, ActorJoinEntrySpotOpRuntime, ActorJoinOpEmptyRuntime,
    ActorJoinOpInnerRuntime, ActorJoinOpReadyRuntime, ActorJoinReplyOpInnerRuntime,
    ActorJoinReplyOpRuntime, ActorLookupOpInnerRuntime, ActorLookupOpRuntime,
    ActorReplyOpInnerRuntime, ActorReplyOpRuntime, ActorReplyOpTimeoutRuntime,
};

// ---------------------------------------------------------------------------
// Actor value structs
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Actor operation builders
// ---------------------------------------------------------------------------

pub(super) struct NativeActorJoinOp {
    pub(super) node_handle: *mut c_void,
    pub(super) spot_handle: *mut c_void,
    pub(super) actor: ffi::zlink_actor_ref_t,
    pub(super) dest_node_rid: RoutingId,
    pub(super) dest_spot_rid: RoutingId,
    pub(super) parts: Vec<Message>,
    pub(super) flags: SendFlags,
    pub(super) timeout: Duration,
}

unsafe impl Send for NativeActorJoinOp {}

impl ActorJoinOpInnerRuntime for NativeActorJoinOp {
    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }

    fn into_any(self: Box<Self>) -> Box<dyn std::any::Any> {
        self
    }
}

pub(super) fn wrap_actor_join_op<State>(inner: NativeActorJoinOp) -> ActorJoinOp<State> {
    ActorJoinOp {
        inner: Box::new(inner),
        _state: std::marker::PhantomData,
    }
}

fn take_actor_join_op<State>(op: ActorJoinOp<State>) -> NativeActorJoinOp {
    *op.inner
        .into_any()
        .downcast::<NativeActorJoinOp>()
        .expect("zlink native actor join op")
}

fn actor_join_op_mut<State>(op: &mut ActorJoinOp<State>) -> &mut NativeActorJoinOp {
    op.inner
        .as_mut()
        .as_any_mut()
        .downcast_mut::<NativeActorJoinOp>()
        .expect("zlink native actor join op")
}

pub(super) struct NativeActorJoinEntrySpotOp {
    pub(super) node_handle: *mut c_void,
    pub(super) actor: ffi::zlink_actor_ref_t,
    pub(super) dest_node_rid: RoutingId,
    pub(super) timeout: Duration,
}

unsafe impl Send for NativeActorJoinEntrySpotOp {}

impl ActorJoinEntrySpotOpInnerRuntime for NativeActorJoinEntrySpotOp {
    fn into_any(self: Box<Self>) -> Box<dyn std::any::Any> {
        self
    }
}

pub(super) fn wrap_actor_join_entry_spot_op<State>(
    inner: NativeActorJoinEntrySpotOp,
) -> ActorJoinEntrySpotOp<State> {
    ActorJoinEntrySpotOp {
        inner: Box::new(inner),
        _state: std::marker::PhantomData,
    }
}

fn take_actor_join_entry_spot_op<State>(
    op: ActorJoinEntrySpotOp<State>,
) -> NativeActorJoinEntrySpotOp {
    *op.inner
        .into_any()
        .downcast::<NativeActorJoinEntrySpotOp>()
        .expect("zlink native actor join entry spot op")
}

pub(super) struct NativeActorJoinReplyOp {
    pub(super) spot_handle: *mut c_void,
    pub(super) info: ffi::zlink_actor_join_info_t,
    pub(super) join_result_code: i32,
    pub(super) parts: Vec<Message>,
}

unsafe impl Send for NativeActorJoinReplyOp {}

impl ActorJoinReplyOpInnerRuntime for NativeActorJoinReplyOp {
    fn into_any(self: Box<Self>) -> Box<dyn std::any::Any> {
        self
    }
}

pub(super) fn wrap_actor_join_reply_op<State>(
    inner: NativeActorJoinReplyOp,
) -> ActorJoinReplyOp<State> {
    ActorJoinReplyOp {
        inner: Box::new(inner),
        _state: std::marker::PhantomData,
    }
}

fn take_actor_join_reply_op<State>(op: ActorJoinReplyOp<State>) -> NativeActorJoinReplyOp {
    *op.inner
        .into_any()
        .downcast::<NativeActorJoinReplyOp>()
        .expect("zlink native actor join reply op")
}

pub(super) struct NativeActorReplyOp {
    pub(super) handle: *mut c_void,
    pub(super) kind: NativeActorReplyOpKind,
    pub(super) timeout: Duration,
}

unsafe impl Send for NativeActorReplyOp {}

impl ActorReplyOpInnerRuntime for NativeActorReplyOp {
    fn into_any(self: Box<Self>) -> Box<dyn std::any::Any> {
        self
    }
}

pub(super) enum NativeActorReplyOpKind {
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

pub(super) fn wrap_actor_reply_op<T, State>(inner: NativeActorReplyOp) -> T
where
    T: FromActorReplyInner<State>,
{
    T::from_actor_reply_inner(inner)
}

pub(super) trait FromActorReplyInner<State> {
    fn from_actor_reply_inner(inner: NativeActorReplyOp) -> Self;
}

macro_rules! impl_from_actor_reply_inner {
    ($ty:ident) => {
        impl<State> FromActorReplyInner<State> for $ty<State> {
            fn from_actor_reply_inner(inner: NativeActorReplyOp) -> Self {
                Self {
                    inner: Box::new(inner),
                    _state: std::marker::PhantomData,
                }
            }
        }
    };
}

impl_from_actor_reply_inner!(ActorLeaveOp);
impl_from_actor_reply_inner!(ActorDestroyOp);
impl_from_actor_reply_inner!(ActorBindOp);
impl_from_actor_reply_inner!(ActorUnbindOp);

fn take_actor_reply_op<State, T>(op: T) -> NativeActorReplyOp
where
    T: IntoActorReplyInner<State>,
{
    T::into_actor_reply_inner(op)
}

trait IntoActorReplyInner<State> {
    fn into_actor_reply_inner(self) -> NativeActorReplyOp;
}

macro_rules! impl_into_actor_reply_inner {
    ($ty:ident) => {
        impl<State> IntoActorReplyInner<State> for $ty<State> {
            fn into_actor_reply_inner(self) -> NativeActorReplyOp {
                *self
                    .inner
                    .into_any()
                    .downcast::<NativeActorReplyOp>()
                    .expect("zlink native actor reply op")
            }
        }
    };
}

impl_into_actor_reply_inner!(ActorLeaveOp);
impl_into_actor_reply_inner!(ActorDestroyOp);
impl_into_actor_reply_inner!(ActorBindOp);
impl_into_actor_reply_inner!(ActorUnbindOp);

pub(super) struct NativeActorLookupOp {
    pub(super) node_handle: *mut c_void,
    pub(super) target_node_rid: RoutingId,
    pub(super) actor_id: std::ffi::CString,
    pub(super) timeout: Duration,
}

unsafe impl Send for NativeActorLookupOp {}

impl ActorLookupOpInnerRuntime for NativeActorLookupOp {
    fn into_any(self: Box<Self>) -> Box<dyn std::any::Any> {
        self
    }
}

pub(super) fn wrap_actor_lookup_op<State>(inner: NativeActorLookupOp) -> ActorLookupOp<State> {
    ActorLookupOp {
        inner: Box::new(inner),
        _state: std::marker::PhantomData,
    }
}

fn take_actor_lookup_op<State>(op: ActorLookupOp<State>) -> NativeActorLookupOp {
    *op.inner
        .into_any()
        .downcast::<NativeActorLookupOp>()
        .expect("zlink native actor lookup op")
}

impl ActorJoinEntrySpotOpRuntime for ActorJoinEntrySpotOp<Empty> {
    fn timeout(self, timeout: Duration) -> Self {
        let mut op = take_actor_join_entry_spot_op(self);
        op.timeout = timeout;
        wrap_actor_join_entry_spot_op(op)
    }

    /// Submit with a completion callback.
    /// # Errors: SubmitError
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinEntrySpotResult) + Send + 'static,
    {
        let state_ptr = Box::into_raw(Box::new(ActorJoinEntrySpotCallbackState {
            callback: Some(Box::new(callback)),
        }));
        let op = take_actor_join_entry_spot_op(self);
        let timeout_ms = timeout_to_timeout_ms(op.timeout);
        let rc = unsafe {
            ffi::zlink_spot_node_actor_join_entry_spot(
                op.node_handle,
                &op.actor,
                op.dest_node_rid.as_raw(),
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

impl ActorJoinOpEmptyRuntime for ActorJoinOp<Empty> {
    fn message(self, message: Message) -> ActorJoinOp<Ready> {
        let op = take_actor_join_op(self);
        wrap_actor_join_op(NativeActorJoinOp {
            node_handle: op.node_handle,
            spot_handle: op.spot_handle,
            actor: op.actor,
            dest_node_rid: op.dest_node_rid,
            dest_spot_rid: op.dest_spot_rid,
            parts: vec![message],
            flags: op.flags,
            timeout: op.timeout,
        })
    }
}

impl ActorJoinOpReadyRuntime for ActorJoinOp<Ready> {
    fn message(mut self, message: Message) -> Self {
        actor_join_op_mut(&mut self).parts.push(message);
        self
    }

    fn timeout(self, timeout: Duration) -> Self {
        let mut op = take_actor_join_op(self);
        op.timeout = timeout;
        wrap_actor_join_op(op)
    }

    fn flags(self, flags: SendFlags) -> Self {
        let mut op = take_actor_join_op(self);
        op.flags = flags;
        wrap_actor_join_op(op)
    }

    /// Submit with a completion callback.
    /// # Errors: SubmitError
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinResult, Vec<Message>) + Send + 'static,
    {
        let mut op = take_actor_join_op(self);
        let native = prepare_send_parts(&mut op.parts)?;
        let mut native = native;
        let state_ptr = Box::into_raw(Box::new(ActorJoinCallbackState {
            callback: Some(Box::new(callback)),
            _progress: if op.spot_handle.is_null() {
                None
            } else {
                Some(RequestProgressGuard::attach_spot(op.spot_handle))
            },
        }));
        let timeout_ms = timeout_to_timeout_ms(op.timeout);
        let flags_bits = op.flags.bits();
        let rc = unsafe {
            ffi::zlink_spot_node_actor_join_spot(
                op.node_handle,
                &op.actor,
                op.dest_node_rid.as_raw(),
                op.dest_spot_rid.as_raw(),
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

impl ActorJoinReplyOpRuntime for ActorJoinReplyOp<Empty> {
    fn message(self, message: Message) -> ActorJoinReplyOp<Empty> {
        let mut op = take_actor_join_reply_op(self);
        op.parts.push(message);
        wrap_actor_join_reply_op(op)
    }

    /// # Errors: SubmitError
    fn submit(self) -> Result<(), SubmitError> {
        let mut op = take_actor_join_reply_op(self);
        // 0..N parts allowed.
        let mut native: Vec<ffi::zlink_msg_t> = Vec::with_capacity(op.parts.len());
        unsafe {
            for part in op.parts.iter_mut() {
                let mut dest = MaybeUninit::<ffi::zlink_msg_t>::uninit();
                ffi::zlink_msg_init(dest.as_mut_ptr());
                ffi::zlink_msg_move(dest.as_mut_ptr(), part.raw_mut());
                native.push(dest.assume_init());
            }
        }
        let rc = unsafe {
            ffi::zlink_spot_actor_join_reply(
                op.spot_handle,
                &op.info,
                op.join_result_code,
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

impl NativeActorReplyOp {
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
            NativeActorReplyOpKind::Leave {
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
            NativeActorReplyOpKind::Destroy { actor } => unsafe {
                ffi::zlink_spot_node_actor_destroy(
                    self.handle,
                    actor,
                    spot_reply_callback,
                    state_ptr.cast(),
                    timeout_ms,
                )
            },
            NativeActorReplyOpKind::Bind { session_rid, actor } => unsafe {
                ffi::zlink_stream_bind_actor(
                    self.handle,
                    session_rid.as_raw(),
                    actor,
                    spot_reply_callback,
                    state_ptr.cast(),
                    timeout_ms,
                )
            },
            NativeActorReplyOpKind::Unbind {
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

impl ActorReplyOpTimeoutRuntime for ActorLeaveOp<Empty> {
    fn timeout(self, timeout: Duration) -> Self {
        let mut op = take_actor_reply_op(self);
        op.timeout = timeout;
        wrap_actor_reply_op(op)
    }
}

impl ActorReplyOpRuntime for ActorLeaveOp<Empty> {
    /// # Errors: SubmitError
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        take_actor_reply_op(self).submit_callback(callback)
    }
}

impl ActorReplyOpTimeoutRuntime for ActorDestroyOp<Empty> {
    fn timeout(self, timeout: Duration) -> Self {
        let mut op = take_actor_reply_op(self);
        op.timeout = timeout;
        wrap_actor_reply_op(op)
    }
}

impl ActorReplyOpRuntime for ActorDestroyOp<Empty> {
    /// # Errors: SubmitError
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        take_actor_reply_op(self).submit_callback(callback)
    }
}

impl ActorReplyOpTimeoutRuntime for ActorBindOp<Empty> {
    fn timeout(self, timeout: Duration) -> Self {
        let mut op = take_actor_reply_op(self);
        op.timeout = timeout;
        wrap_actor_reply_op(op)
    }
}

impl ActorReplyOpRuntime for ActorBindOp<Empty> {
    /// # Errors: SubmitError
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        take_actor_reply_op(self).submit_callback(callback)
    }
}

impl ActorReplyOpTimeoutRuntime for ActorUnbindOp<Empty> {
    fn timeout(self, timeout: Duration) -> Self {
        let mut op = take_actor_reply_op(self);
        op.timeout = timeout;
        wrap_actor_reply_op(op)
    }
}

impl ActorReplyOpRuntime for ActorUnbindOp<Empty> {
    /// # Errors: SubmitError
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        take_actor_reply_op(self).submit_callback(callback)
    }
}

impl ActorLookupOpRuntime for ActorLookupOp<Empty> {
    fn timeout(self, timeout: Duration) -> Self {
        let mut op = take_actor_lookup_op(self);
        op.timeout = timeout;
        wrap_actor_lookup_op(op)
    }

    /// # Errors: SubmitError
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorLookupResult) + Send + 'static,
    {
        let op = take_actor_lookup_op(self);
        let state_ptr = Box::into_raw(Box::new(ActorLookupCallbackState {
            callback: Some(Box::new(callback)),
        }));
        let rc = unsafe {
            ffi::zlink_remote_actor_get_ref(
                op.node_handle,
                op.target_node_rid.as_raw(),
                op.actor_id.as_ptr(),
                actor_lookup_user_callback,
                state_ptr.cast(),
                timeout_to_timeout_ms(op.timeout),
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
                node_rid: RoutingId::from_raw(ffi::zlink_routing_id_t::empty()),
                actor_id: String::new(),
                generation: 0,
            },
            joined_spot_rid: RoutingId::from_raw(ffi::zlink_routing_id_t::empty()),
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
                node_rid: RoutingId::from_raw(ffi::zlink_routing_id_t::empty()),
                actor_id: String::new(),
                generation: 0,
            },
            target_node_rid: RoutingId::from_raw(ffi::zlink_routing_id_t::empty()),
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
                node_rid: RoutingId::from_raw(ffi::zlink_routing_id_t::empty()),
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
