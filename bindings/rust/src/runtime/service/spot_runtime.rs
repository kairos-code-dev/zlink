use super::*;
use crate::runtime_bridge::SpotPublicRuntime;

// ---------------------------------------------------------------------------
// Spot
// ---------------------------------------------------------------------------

/// Unified SPOT facade over an existing SPOT node.
///
/// The Spot handle borrows a `SpotNode`, lazily creates side sockets, and
/// exposes the canonical data-plane API (publish, subscribe, etc.).
pub(super) struct NativeSpot {
    pub(super) handle: *mut c_void,
    node_handle: *mut c_void,
    send_ready_cb: Option<CallbackBox>,
    dispatch_cb: Option<CallbackBox>,
}

unsafe impl Send for NativeSpot {}

impl SpotRuntime for NativeSpot {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

pub(crate) fn spot_handle(spot: &Spot) -> *mut c_void {
    spot.inner
        .as_any()
        .downcast_ref::<NativeSpot>()
        .expect("zlink native spot")
        .handle
}

pub(super) fn spot_node_raw(spot: &Spot) -> *mut c_void {
    spot.inner
        .as_any()
        .downcast_ref::<NativeSpot>()
        .expect("zlink native spot")
        .node_handle
}

pub(super) fn spot_native_mut(spot: &mut Spot) -> &mut NativeSpot {
    spot.inner
        .as_any_mut()
        .downcast_mut::<NativeSpot>()
        .expect("zlink native spot")
}

pub(super) fn wrap_spot(handle: *mut c_void, node_handle: *mut c_void) -> Spot {
    Spot {
        inner: Box::new(NativeSpot {
            handle,
            node_handle,
            send_ready_cb: None,
            dispatch_cb: None,
        }),
    }
}

impl Spot {
    pub(crate) fn new(node: &SpotNode) -> Result<Self, ConfigError> {
        let handle = unsafe { ffi::zlink_spot_new(node.raw()) };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(wrap_spot(handle, node.raw()))
    }
}

impl SpotPublicRuntime for Spot {
    fn publish(&self, topic: &str) -> SendOp<Empty> {
        let c_topic = fixed_cstring_or_panic(topic, "topic");
        wrap_send_op(NativeSendOp {
            handle: spot_handle(self),
            kind: SendOpKind::Publish { topic: c_topic },
            parts: Vec::new(),
            flags: SendFlags::NONE,
        })
    }

    fn send_to_channel(&self, channel_name: &str) -> SendOp<Empty> {
        let c_channel_name = fixed_cstring_or_panic(channel_name, "channel_name");
        wrap_send_op(NativeSendOp {
            handle: spot_handle(self),
            kind: SendOpKind::SendToChannel {
                channel_name: c_channel_name,
            },
            parts: Vec::new(),
            flags: SendFlags::NONE,
        })
    }

    fn send_to_spot(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId) -> SendOp<Empty> {
        wrap_send_op(NativeSendOp {
            handle: spot_handle(self),
            kind: SendOpKind::SendToSpot {
                dest_node_rid,
                dest_spot_rid,
            },
            parts: Vec::new(),
            flags: SendFlags::NONE,
        })
    }

    fn request_to_channel(&self, channel_name: &str) -> RequestOp<Empty> {
        let c_channel_name = fixed_cstring_or_panic(channel_name, "channel_name");
        wrap_request_op(NativeRequestOp {
            handle: spot_handle(self),
            kind: RequestOpKind::Channel {
                channel_name: c_channel_name,
            },
            parts: Vec::new(),
            flags: None,
            timeout: Duration::ZERO,
        })
    }

    fn request_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    ) -> RequestOp<Empty> {
        wrap_request_op(NativeRequestOp {
            handle: spot_handle(self),
            kind: RequestOpKind::ToSpot {
                dest_node_rid,
                dest_spot_rid,
            },
            parts: Vec::new(),
            flags: None,
            timeout: Duration::ZERO,
        })
    }

    fn request_to_router(&self, peer_rid: RoutingId) -> RequestOp<Empty> {
        wrap_request_op(NativeRequestOp {
            handle: spot_handle(self),
            kind: RequestOpKind::ToRouter { peer_rid },
            parts: Vec::new(),
            flags: None,
            timeout: Duration::ZERO,
        })
    }

    fn reply_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
    ) -> ReplyOp<Empty> {
        wrap_reply_op(NativeReplyOp {
            handle: spot_handle(self),
            kind: ReplyOpKind::ToSpot {
                dest_node_rid,
                dest_spot_rid,
                request_seq,
            },
            parts: Vec::new(),
            flags: SendFlags::NONE,
        })
    }

    fn reply_to_router(&self, peer_rid: RoutingId, request_seq: u64) -> ReplyOp<Empty> {
        wrap_reply_op(NativeReplyOp {
            handle: spot_handle(self),
            kind: ReplyOpKind::ToRouter {
                peer_rid,
                request_seq,
            },
            parts: Vec::new(),
            flags: SendFlags::NONE,
        })
    }

    fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_routing_id(
                spot_handle(self),
                rid.data().as_ptr() as *const c_void,
                rid.len(),
            )
        })
    }

    fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        let mut raw = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        check_config_rc(unsafe { ffi::zlink_get_routing_id(spot_handle(self), raw.as_mut_ptr()) })?;
        Ok(RoutingId::from_raw(unsafe { raw.assume_init() }))
    }

    fn set_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        let c = CString::new(filter).map_err(|_| config_validation_error())?;
        check_config_rc(unsafe { ffi::zlink_set_subscription(spot_handle(self), c.as_ptr()) })
    }

    fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        let c = CString::new(filter).map_err(|_| config_validation_error())?;
        check_config_rc(unsafe { ffi::zlink_unset_subscription(spot_handle(self), c.as_ptr()) })
    }

    fn subscribe(&self, out: &mut TopicMessage, flags: RecvFlags) -> Result<bool, RecvError> {
        let mut topic_buf = [0i8; 256];
        match recv_spot_subscribed_parts(spot_handle(self), &mut topic_buf, flags.bits())? {
            Some((routing_id, topic, parts)) => {
                out.adopt_from(TopicMessage::new(routing_id, topic, parts));
                Ok(true)
            }
            None => Ok(false),
        }
    }

    fn receive_subscription_event(
        &self,
        _out: &mut SubscriptionEvent,
        _flags: RecvFlags,
    ) -> Result<bool, RecvError> {
        Err(RecvError::new(
            crate::error::RecvResult::NotSupported,
            libc::ENOTSUP,
        ))
    }

    fn recv_actor_join_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<Option<ActorJoinRequest>, RecvError> {
        let mut raw_info = MaybeUninit::<ffi::zlink_actor_join_info_t>::zeroed();
        let mut parts: *mut ffi::zlink_msg_t = ptr::null_mut();
        let mut part_count: usize = 0;
        let rc = unsafe {
            ffi::zlink_spot_actor_join_recv(
                spot_handle(self),
                raw_info.as_mut_ptr(),
                &mut parts,
                &mut part_count,
                flags.bits(),
            )
        };
        if rc != 0 {
            if last_errno() == libc::EAGAIN && flags == RecvFlags::DONT_WAIT {
                return Ok(None);
            }
            return Err(check_recv_rc(rc).unwrap_err());
        }
        let info = ActorJoinInfo::from_raw(unsafe { raw_info.assume_init() });
        let messages = take_parts(parts, part_count);
        unsafe {
            ffi::zlink_multipart_close(parts, part_count);
        }
        let message = messages
            .into_iter()
            .next()
            .unwrap_or_else(|| Message::new().expect("empty zlink message init failed"));
        Ok(Some(ActorJoinRequest { info, message }))
    }

    fn recv_actor_join(&self) -> Result<ActorJoinRequest, RecvError> {
        self.recv_actor_join_with_flags(RecvFlags::NONE)?
            .ok_or_else(|| RecvError::new(RecvResult::NoData, libc::EAGAIN))
    }

    /// Reply to an Actor join admission request (operation builder).
    /// Multipart reply payload accumulates via `.message(...)`. A zero-message
    /// `submit()` is allowed.
    fn reply_actor_join(
        &self,
        request: &ActorJoinRequest,
        join_result_code: i32,
    ) -> ActorJoinReplyOp<Empty> {
        wrap_actor_join_reply_op(NativeActorJoinReplyOp {
            spot_handle: spot_handle(self),
            info: actor_join_info_to_raw(&request.info),
            join_result_code,
            parts: Vec::new(),
        })
    }

    fn actors(&self) -> Result<Vec<ActorRef>, ConfigError> {
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_spot_actors(spot_handle(self), ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }
        let mut entries = vec![unsafe { std::mem::zeroed::<ffi::zlink_actor_ref_t>() }; count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_spot_actors(spot_handle(self), entries_ptr, count_ptr)
            },
            entries.as_mut_ptr(),
        )?;
        Ok(entries[..actual].iter().map(ActorRef::from_raw).collect())
    }

    fn on_dispatch_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: for<'a> Fn(SpotDispatchInfo<'a>) + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new((spot_node_raw(self), handler));
        let rc = unsafe {
            ffi::zlink_spot_dispatch_event_handler(
                spot_handle(self),
                spot_dispatch_trampoline::<F>,
                userdata,
            )
        };
        if rc != 0 {
            drop(cb);
            return Err(check_handler_rc(rc).unwrap_err());
        }
        spot_native_mut(self).dispatch_cb = Some(cb);
        Ok(())
    }

    fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new(handler);
        let rc = unsafe {
            ffi::zlink_send_ready_handler(spot_handle(self), send_ready_trampoline::<F>, userdata)
        };
        if rc != 0 {
            drop(cb);
            return Err(check_handler_rc(rc).unwrap_err());
        }
        spot_native_mut(self).send_ready_cb = Some(cb);
        Ok(())
    }

    fn recv_actor_lifecycle_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<Option<SpotActorLifecycleEvent>, RecvError> {
        let mut raw_event = MaybeUninit::<ffi::zlink_spot_actor_lifecycle_event_t>::zeroed();
        let rc = unsafe {
            ffi::zlink_spot_recv_actor_lifecycle(
                spot_handle(self),
                raw_event.as_mut_ptr(),
                flags.bits(),
            )
        };
        if rc != 0 {
            if last_errno() == libc::EAGAIN && flags == RecvFlags::DONT_WAIT {
                return Ok(None);
            }
            return Err(check_recv_rc(rc).unwrap_err());
        }
        let raw_event = unsafe { raw_event.assume_init() };
        Ok(Some(SpotActorLifecycleEvent {
            kind: SpotActorLifecycleEventKind::from_raw(raw_event.kind),
            info: SpotActorLifecycleInfo::from_raw(raw_event.info),
        }))
    }

    fn recv_actor_lifecycle(&self) -> Result<SpotActorLifecycleEvent, RecvError> {
        self.recv_actor_lifecycle_with_flags(RecvFlags::NONE)?
            .ok_or_else(|| RecvError::new(RecvResult::NoData, libc::EAGAIN))
    }

    /// Receive a routed message, blocking until one is available.
    fn recv_routed(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        match recv_spot_routed_parts(spot_handle(self), flags.bits())? {
            Some((node_rid, spot_rid, req_seq, parts)) => {
                out.adopt_from(spot_received_from_raw(
                    spot_handle(self),
                    node_rid,
                    spot_rid,
                    req_seq,
                    parts,
                ));
                Ok(true)
            }
            None => Ok(false),
        }
    }

    fn close(&mut self) -> Result<(), CloseError> {
        destroy_handle_close(&mut spot_handle(self), ffi::zlink_spot_destroy)
    }
}

type SpotReplyCallback = Box<dyn FnOnce(Result<Vec<Message>, RequestError>) + Send>;
type SpotDispatchHandler<F> = (*mut c_void, F);

pub(super) struct SpotReplyCallbackState {
    pub(super) callback: Option<SpotReplyCallback>,
    pub(super) _progress: Option<RequestProgressGuard>,
}

pub(crate) fn spot_dispatch_info_recv_actor(
    info: &SpotDispatchInfo<'_>,
    out: &mut ActorReceived,
    flags: RecvFlags,
) -> Result<bool, RecvError> {
    if info.event != SpotDispatchEvent::ActorReadable {
        return Err(RecvError::new(RecvResult::NotSupported, libc::ENOTSUP));
    }
    let actor = info
        .actor_for_recv
        .as_ref()
        .ok_or_else(|| RecvError::new(RecvResult::InvalidHandle, libc::EFAULT))?;
    match recv_actor_once(info.node_cookie as *mut c_void, actor, flags)? {
        Some(received) => {
            out.adopt_from(received);
            Ok(true)
        }
        None => Ok(false),
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum SpotDispatchSubjectKind {
    Spot,
    Timer,
    ChannelDealer,
    Actor,
}

impl SpotDispatchSubjectKind {
    fn from_raw(raw: ffi::zlink_spot_dispatch_subject_kind_t) -> Self {
        match raw {
            ffi::zlink_spot_dispatch_subject_kind_t::ZLINK_SPOT_DISPATCH_SUBJECT_SPOT => Self::Spot,
            ffi::zlink_spot_dispatch_subject_kind_t::ZLINK_SPOT_DISPATCH_SUBJECT_TIMER => {
                Self::Timer
            }
            ffi::zlink_spot_dispatch_subject_kind_t::ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER => {
                Self::ChannelDealer
            }
            ffi::zlink_spot_dispatch_subject_kind_t::ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR => {
                Self::Actor
            }
        }
    }
}

impl SpotDispatchEvent {
    fn from_raw(raw: ffi::zlink_spot_dispatch_event_t) -> Self {
        match raw {
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE => {
                Self::SubscribeReadable
            }
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE => {
                Self::RoutedReadable
            }
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE => {
                Self::TimerReadable
            }
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE => {
                Self::ChannelReplyReadable
            }
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE => {
                Self::ActorReadable
            }
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE => {
                Self::ActorJoinReadable
            }
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_ACTOR_LIFECYCLE_READABLE => {
                Self::ActorLifecycleReadable
            }
        }
    }
}

pub(super) fn timeout_to_timeout_ms(timeout: Duration) -> u32 {
    let millis = timeout.as_millis();
    if millis == 0 {
        0
    } else {
        millis.min(u32::MAX as u128) as u32
    }
}

pub(crate) fn request_result_from_raw(
    raw: ffi::zlink_request_result_t,
) -> crate::error::RequestResult {
    match raw {
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK => crate::error::RequestResult::Ok,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_TIMED_OUT => {
            crate::error::RequestResult::TimedOut
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_NOT_FOUND => {
            crate::error::RequestResult::NotFound
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_TERMINATED => {
            crate::error::RequestResult::Terminated
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_PROTOCOL_ERROR => {
            crate::error::RequestResult::ProtocolError
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_INTERNAL_ERROR => {
            crate::error::RequestResult::InternalError
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_REJECTED => {
            crate::error::RequestResult::Rejected
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_CONFLICT => {
            crate::error::RequestResult::Conflict
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_BUSY => crate::error::RequestResult::Busy,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_NOT_CONNECTED => {
            crate::error::RequestResult::NotConnected
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_INVALID_ARGUMENT => {
            crate::error::RequestResult::InvalidArgument
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_INVALID_STATE => {
            crate::error::RequestResult::InvalidState
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_NOT_SUPPORTED => {
            crate::error::RequestResult::NotSupported
        }
    }
}

pub(crate) fn check_request_result(raw: ffi::zlink_request_result_t) -> Result<(), RequestError> {
    let code = request_result_from_raw(raw);
    if code == crate::error::RequestResult::Ok {
        Ok(())
    } else {
        Err(request_error_from_result(code))
    }
}

pub(crate) fn wait_reply_submit<F>(submit: F) -> Result<(), RequestError>
where
    F: FnOnce(ffi::zlink_reply_handler_fn, *mut c_void) -> ffi::zlink_submit_result_t,
{
    let (tx, rx) = mpsc::channel();
    let state = Box::into_raw(Box::new(tx));
    let rc = submit(reply_result_callback, state.cast());
    if rc != 0 {
        unsafe {
            drop(Box::from_raw(state));
        }
        return Err(request_error_from_submit(check_submit_rc(rc).unwrap_err()));
    }
    let result = rx
        .recv()
        .map_err(|_| request_error_from_result(crate::error::RequestResult::InternalError))?;
    check_request_result(result)
}

pub(super) fn recv_error_from_config(err: ConfigError) -> RecvError {
    RecvError::new(RecvResult::InvalidHandle, err.native_errno())
}

struct ActorPart {
    info: ActorRecvInfo,
    message: Message,
    more: bool,
}

pub(super) fn recv_actor_once(
    node: *mut c_void,
    actor: &ActorRef,
    flags: RecvFlags,
) -> Result<Option<ActorReceived>, RecvError> {
    let mut parts = Vec::new();
    let mut recv_flags = flags;
    let mut info = None;

    loop {
        match recv_actor_part_from_ref(node, actor, recv_flags)? {
            Some(part) => {
                let more = part.more;
                if info.is_none() {
                    info = Some(part.info);
                }
                parts.push(part.message);
                if !more {
                    let info =
                        info.ok_or_else(|| RecvError::new(RecvResult::NoData, libc::EAGAIN))?;
                    return Ok(Some(ActorReceived::new(info, parts)));
                }
                recv_flags = RecvFlags::DONT_WAIT;
            }
            None if parts.is_empty() => return Ok(None),
            None => return Err(RecvError::new(RecvResult::NoData, libc::EAGAIN)),
        }
    }
}

fn recv_actor_part_from_ref(
    node: *mut c_void,
    actor: &ActorRef,
    flags: RecvFlags,
) -> Result<Option<ActorPart>, RecvError> {
    let raw_actor = actor.to_raw().map_err(recv_error_from_config)?;
    let mut raw_info = MaybeUninit::<ffi::zlink_actor_recv_info_t>::zeroed();
    let mut raw_part = MaybeUninit::<ffi::zlink_msg_t>::uninit();
    let mut has_more = ffi::zlink_part_flag_t::ZLINK_PART_FINAL;
    let rc = unsafe {
        ffi::zlink_spot_node_actor_recv_part(
            node,
            &raw_actor,
            raw_info.as_mut_ptr(),
            raw_part.as_mut_ptr(),
            &mut has_more,
            flags.bits(),
        )
    };
    if rc != 0 {
        if last_errno() == libc::EAGAIN && flags == RecvFlags::DONT_WAIT {
            return Ok(None);
        }
        return Err(check_recv_rc(rc).unwrap_err());
    }
    let info = ActorRecvInfo::from_raw(&unsafe { raw_info.assume_init() });
    let message = unsafe { Message::from_raw(raw_part.assume_init()) };
    Ok(Some(ActorPart {
        info,
        message,
        more: has_more == ffi::zlink_part_flag_t::ZLINK_PART_MORE,
    }))
}

pub(super) fn take_message_raw(message: &mut Message) -> ffi::zlink_msg_t {
    unsafe {
        let mut native = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        ffi::zlink_msg_init(native.as_mut_ptr());
        ffi::zlink_msg_move(native.as_mut_ptr(), message.raw_mut());
        native.assume_init()
    }
}

fn spot_received_from_raw(
    handle: *mut c_void,
    source_node_rid: *const ffi::zlink_routing_id_t,
    source_spot_rid: *const ffi::zlink_routing_id_t,
    request_seq: u64,
    parts: Vec<Message>,
) -> Received {
    let node_rid = if source_node_rid.is_null() {
        RoutingId::from_raw(ffi::zlink_routing_id_t {
            size: 0,
            data: [0; 255],
        })
    } else {
        unsafe { RoutingId::from_raw(*source_node_rid) }
    };
    let spot_rid = if source_spot_rid.is_null() {
        None
    } else {
        let rid = unsafe { RoutingId::from_raw(*source_spot_rid) };
        if rid.is_empty() { None } else { Some(rid) }
    };
    if let Some(spot_rid) = spot_rid {
        if request_seq == 0 {
            Received::with_spot_send_context(handle, node_rid, spot_rid, parts)
        } else {
            Received::with_spot_reply_and_send_context(
                handle,
                node_rid,
                spot_rid,
                request_seq,
                parts,
            )
        }
    } else if request_seq == 0 {
        Received::new(Some(node_rid), parts)
    } else {
        Received::with_spot_router_reply_context(handle, node_rid, request_seq, parts)
    }
}

pub(super) unsafe extern "C" fn spot_reply_callback(
    result_: ffi::zlink_request_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let mut state = unsafe { Box::from_raw(userdata.cast::<SpotReplyCallbackState>()) };
    let callback = state.callback.take().expect("spot request callback");
    if result_ == ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK {
        callback(Ok(borrowed_parts_to_messages(parts, part_count)));
    } else {
        callback(Err(request_error_from_result(request_result_from_raw(
            result_,
        ))));
    }
}

unsafe extern "C" fn reply_result_callback(
    result_: ffi::zlink_request_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let sender =
        unsafe { Box::from_raw(userdata.cast::<mpsc::Sender<ffi::zlink_request_result_t>>()) };
    let _ = sender.send(result_);
    if !parts.is_null() {
        for i in 0..part_count {
            unsafe {
                ffi::zlink_msg_close(parts.add(i));
            }
        }
    }
}

unsafe extern "C" fn spot_dispatch_trampoline<
    F: for<'a> Fn(SpotDispatchInfo<'a>) + Send + 'static,
>(
    _spot: *mut c_void,
    info: *const ffi::zlink_spot_dispatch_info_t,
    userdata: *mut c_void,
) {
    let state = unsafe { &*(userdata as *const SpotDispatchHandler<F>) };
    let node_handle = state.0;
    let handler = &state.1;
    let info = unsafe { &*info };
    let event = SpotDispatchEvent::from_raw(info.event);
    let subject_kind = SpotDispatchSubjectKind::from_raw(info.subject_kind);

    let actor_for_recv = match subject_kind {
        SpotDispatchSubjectKind::Actor if !info.subject.is_null() => {
            Some(ActorRef::from_raw(unsafe {
                &*(info.subject as *const ffi::zlink_actor_ref_t)
            }))
        }
        _ => None,
    };
    handler(SpotDispatchInfo::new(
        event,
        node_handle as usize,
        actor_for_recv,
    ));
}
