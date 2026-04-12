use std::ffi::c_void;
use std::ptr;
use std::sync::mpsc;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use std::thread::JoinHandle;
use std::time::Duration;

use crate::domain::{Received, SendResult};
use crate::error::{RecvResult, RequestResult, ZlinkError, check_rc};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::{IntoMultipart, Message, RoutingId};
use crate::socket::{DealerSocket, RouterSocket, prepare_send_parts, take_parts};

const DEFAULT_TIMEOUT: Duration = Duration::from_secs(5);

struct ReplyCallbackState {
    tx: mpsc::Sender<Result<Received, ZlinkError>>,
}

pub struct RequestDealer {
    socket: DealerSocket,
    default_timeout: Mutex<Duration>,
}

pub struct RequestRouter {
    socket: RouterSocket,
    default_timeout: Mutex<Duration>,
    stop_flag: Arc<AtomicBool>,
    callback_thread: Mutex<Option<JoinHandle<()>>>,
}

unsafe impl Send for RequestDealer {}
unsafe impl Sync for RequestDealer {}
unsafe impl Send for RequestRouter {}
unsafe impl Sync for RequestRouter {}

impl Drop for RequestRouter {
    fn drop(&mut self) {
        self.stop_flag.store(true, Ordering::SeqCst);
        let _ = self.socket.close();
        if let Some(thread) = self.callback_thread.lock().unwrap().take() {
            let _ = thread.join();
        }
    }
}

impl RequestDealer {
    pub fn new(socket: DealerSocket) -> Result<Self, ZlinkError> {
        Ok(Self {
            socket,
            default_timeout: Mutex::new(DEFAULT_TIMEOUT),
        })
    }

    pub fn set_default_request_timeout(&self, timeout: Duration) {
        *self.default_timeout.lock().unwrap() = timeout;
    }

    pub fn get_default_request_timeout(&self) -> Duration {
        *self.default_timeout.lock().unwrap()
    }

    pub async fn request(&self, msg: Message) -> Result<Received, ZlinkError> {
        self.request_with_timeout(msg, self.get_default_request_timeout())
            .await
    }

    pub async fn request_parts(
        &self,
        parts: impl IntoMultipart,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        self.request_impl_parts(parts, timeout, SendFlags::NONE)
    }

    pub async fn request_with_timeout(
        &self,
        msg: Message,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        self.request_impl(msg, timeout)
    }

    pub fn request_callback<F>(&self, msg: Message, callback: F)
    where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        self.request_callback_with_timeout(msg, callback, self.get_default_request_timeout());
    }

    pub fn request_callback_with_flags<F>(
        &self,
        parts: impl IntoMultipart,
        callback: F,
        flags: SendFlags,
        timeout: Duration,
    ) -> Result<(), ZlinkError>
    where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        callback(self.request_impl_parts(parts, timeout, flags));
        Ok(())
    }

    pub fn request_callback_with_timeout<F>(
        &self,
        msg: Message,
        callback: F,
        timeout: Duration,
    ) where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        callback(self.request_impl(msg, timeout));
    }

    pub async fn try_request(&self, msg: Message) -> Result<Received, ZlinkError> {
        self.try_request_with_timeout(msg, self.get_default_request_timeout())
            .await
    }

    pub async fn try_request_with_timeout(
        &self,
        msg: Message,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        self.request_impl(msg, timeout)
    }

    pub fn try_request_callback<F>(&self, msg: Message, callback: F)
    where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        self.try_request_callback_with_timeout(msg, callback, self.get_default_request_timeout());
    }

    pub fn try_request_callback_with_timeout<F>(
        &self,
        msg: Message,
        callback: F,
        timeout: Duration,
    ) where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        callback(self.request_impl(msg, timeout));
    }

    pub fn recv(&self) -> Result<Received, ZlinkError> {
        self.socket.recv()
    }

    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, ZlinkError> {
        self.socket.recv_with_flags(flags)
    }

    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError> {
        self.socket.try_recv()
    }

    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), ZlinkError>
    where
        F: Fn(Received) + Send + 'static,
    {
        self.socket.on_receive(handler)
    }

    fn request_impl(&self, msg: Message, timeout: Duration) -> Result<Received, ZlinkError> {
        self.request_impl_parts(msg, timeout, SendFlags::NONE)
    }

    fn request_impl_parts(
        &self,
        parts: impl IntoMultipart,
        timeout: Duration,
        flags: SendFlags,
    ) -> Result<Received, ZlinkError> {
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let (tx, rx) = mpsc::channel();
        let state_ptr = Box::into_raw(Box::new(ReplyCallbackState { tx }));
        let rc = unsafe {
            ffi::zlink_dealer_request(
                self.socket.inner.handle,
                native.as_mut_ptr(),
                native.len(),
                dealer_reply_callback,
                state_ptr.cast(),
                flags.bits(),
                duration_to_timeout_ms(timeout),
            )
        };
        if rc != 0 {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
            return Err(ZlinkError::last());
        }
        rx.recv()
            .unwrap_or_else(|_| Err(ZlinkError::state("request callback dropped")))
    }
}

impl RequestRouter {
    pub fn new(socket: RouterSocket) -> Result<Self, ZlinkError> {
        Ok(Self {
            socket,
            default_timeout: Mutex::new(DEFAULT_TIMEOUT),
            stop_flag: Arc::new(AtomicBool::new(false)),
            callback_thread: Mutex::new(None),
        })
    }

    pub fn set_default_request_timeout(&self, timeout: Duration) {
        *self.default_timeout.lock().unwrap() = timeout;
    }

    pub fn get_default_request_timeout(&self) -> Duration {
        *self.default_timeout.lock().unwrap()
    }

    pub async fn request(
        &self,
        routing_id: &RoutingId,
        msg: Message,
    ) -> Result<Received, ZlinkError> {
        self.request_with_timeout(routing_id, msg, self.get_default_request_timeout())
            .await
    }

    pub async fn request_parts(
        &self,
        routing_id: RoutingId,
        parts: impl IntoMultipart,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        self.request_impl_parts(&routing_id, parts, timeout, SendFlags::NONE)
    }

    pub async fn request_with_timeout(
        &self,
        routing_id: &RoutingId,
        msg: Message,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        self.request_impl(routing_id, msg, timeout)
    }

    pub async fn try_request(
        &self,
        routing_id: &RoutingId,
        msg: Message,
    ) -> Result<Received, ZlinkError> {
        self.try_request_with_timeout(routing_id, msg, self.get_default_request_timeout())
            .await
    }

    pub async fn try_request_with_timeout(
        &self,
        routing_id: &RoutingId,
        msg: Message,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        self.request_impl(routing_id, msg, timeout)
    }

    pub fn request_callback<F>(&self, routing_id: RoutingId, msg: Message, callback: F)
    where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        self.request_callback_with_timeout(
            routing_id,
            msg,
            callback,
            self.get_default_request_timeout(),
        );
    }

    pub fn request_callback_with_flags<F>(
        &self,
        routing_id: RoutingId,
        parts: impl IntoMultipart,
        callback: F,
        flags: SendFlags,
        timeout: Duration,
    ) -> Result<(), ZlinkError>
    where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        callback(self.request_impl_parts(&routing_id, parts, timeout, flags));
        Ok(())
    }

    pub fn request_callback_with_timeout<F>(
        &self,
        routing_id: RoutingId,
        msg: Message,
        callback: F,
        timeout: Duration,
    ) where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        callback(self.request_impl(&routing_id, msg, timeout));
    }

    pub fn try_request_callback<F>(&self, routing_id: RoutingId, msg: Message, callback: F)
    where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        self.try_request_callback_with_timeout(
            routing_id,
            msg,
            callback,
            self.get_default_request_timeout(),
        );
    }

    pub fn try_request_callback_with_timeout<F>(
        &self,
        routing_id: RoutingId,
        msg: Message,
        callback: F,
        timeout: Duration,
    ) where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        callback(self.request_impl(&routing_id, msg, timeout));
    }

    pub fn reply(
        &self,
        routing_id: &RoutingId,
        request_seq: u64,
        msg: Message,
    ) -> Result<(), ZlinkError> {
        self.reply_with_flags(routing_id, request_seq, msg, SendFlags::NONE)
    }

    pub fn reply_with_flags(
        &self,
        routing_id: &RoutingId,
        request_seq: u64,
        parts: impl IntoMultipart,
        _flags: SendFlags,
    ) -> Result<(), ZlinkError> {
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = unsafe {
            ffi::zlink_router_reply(
                self.socket.inner.handle,
                routing_id.as_raw(),
                request_seq,
                native.as_mut_ptr(),
                native.len(),
            )
        };
        check_rc(rc)
    }

    pub fn try_reply(
        &self,
        routing_id: &RoutingId,
        request_seq: u64,
        msg: Message,
    ) -> Result<SendResult, ZlinkError> {
        self.reply(routing_id, request_seq, msg)?;
        Ok(SendResult::Sent)
    }

    pub fn recv(&self) -> Result<Received, ZlinkError> {
        self.recv_with_flags(RecvFlags::NONE)
    }

    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, ZlinkError> {
        router_recv(self.socket.inner.handle, flags.bits())?
            .ok_or_else(|| ZlinkError::native(RecvResult::NoData as i32, "no data"))
    }

    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError> {
        router_recv(self.socket.inner.handle, ffi::ZLINK_DONTWAIT)
    }

    pub fn on_receive<F>(&self, handler: F) -> Result<(), ZlinkError>
    where
        F: Fn(Received) + Send + 'static,
    {
        let mut slot = self.callback_thread.lock().unwrap();
        if slot.is_some() {
            return Err(ZlinkError::state("request router callback already installed"));
        }
        let handle = self.socket.inner.handle as usize;
        let stop_flag = Arc::clone(&self.stop_flag);
        *slot = Some(std::thread::spawn(move || loop {
            if stop_flag.load(Ordering::SeqCst) {
                return;
            }
            match router_recv(handle as *mut c_void, 0) {
                Ok(Some(received)) => handler(received),
                Ok(None) => continue,
                Err(_) => return,
            }
        }));
        Ok(())
    }

    fn request_impl(
        &self,
        routing_id: &RoutingId,
        msg: Message,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        self.request_impl_parts(routing_id, msg, timeout, SendFlags::NONE)
    }

    fn request_impl_parts(
        &self,
        routing_id: &RoutingId,
        parts: impl IntoMultipart,
        timeout: Duration,
        flags: SendFlags,
    ) -> Result<Received, ZlinkError> {
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let (tx, rx) = mpsc::channel();
        let state_ptr = Box::into_raw(Box::new(ReplyCallbackState { tx }));
        let rc = unsafe {
            ffi::zlink_router_request(
                self.socket.inner.handle,
                routing_id.as_raw(),
                native.as_mut_ptr(),
                native.len(),
                dealer_reply_callback,
                state_ptr.cast(),
                flags.bits(),
                duration_to_timeout_ms(timeout),
            )
        };
        if rc != 0 {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
            return Err(ZlinkError::last());
        }
        rx.recv()
            .unwrap_or_else(|_| Err(ZlinkError::state("request callback dropped")))
    }
}

fn duration_to_timeout_ms(timeout: Duration) -> u32 {
    let millis = timeout.as_millis();
    if millis == 0 {
        0
    } else {
        millis.min(u32::MAX as u128) as u32
    }
}

fn empty_routing_id() -> RoutingId {
    RoutingId::from_raw(ffi::zlink_routing_id_t {
        size: 0,
        data: [0; 255],
    })
}

fn borrowed_parts_to_owned(
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
) -> Result<Vec<Message>, ZlinkError> {
    let mut out = Vec::with_capacity(part_count);
    for i in 0..part_count {
        let part = unsafe { parts.add(i) };
        let size = unsafe { ffi::zlink_msg_size(part.cast_const()) };
        let data = unsafe { ffi::zlink_msg_data(part) } as *const u8;
        let bytes = if data.is_null() || size == 0 {
            &[][..]
        } else {
            unsafe { std::slice::from_raw_parts(data, size) }
        };
        out.push(Message::from_bytes(bytes)?);
    }
    Ok(out)
}

fn router_recv(
    handle: *mut c_void,
    flags: u32,
) -> Result<Option<Received>, ZlinkError> {
    let mut peer_rid = ptr::null();
    let mut request_seq = 0u64;
    let mut parts_ptr: *mut ffi::zlink_msg_t = ptr::null_mut();
    let mut part_count: usize = 0;
    let rc = unsafe {
        ffi::zlink_router_recv(
            handle,
            &mut peer_rid,
            &mut request_seq,
            &mut parts_ptr,
            &mut part_count,
            flags,
        )
    };
    if rc == RecvResult::NoData as i32 {
        return Ok(None);
    }
    if rc != 0 {
        return Err(ZlinkError::last());
    }

    let routing_id = if peer_rid.is_null() {
        empty_routing_id()
    } else {
        unsafe { RoutingId::from_raw(*peer_rid) }
    };
    let parts = take_parts(parts_ptr, part_count);
    let received = if request_seq == 0 {
        Received::new(routing_id, parts)
    } else {
        Received::with_request_seq(routing_id, parts, request_seq)
    };
    Ok(Some(received))
}

unsafe extern "C" fn dealer_reply_callback(
    result_: i32,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let state = unsafe { Box::from_raw(userdata.cast::<ReplyCallbackState>()) };
    let result = if result_ == RequestResult::Ok as i32 {
        state.tx.send(
            borrowed_parts_to_owned(parts, part_count)
                .map(|owned| Received::new(empty_routing_id(), owned)),
        )
    } else {
        state
            .tx
            .send(Err(ZlinkError::native(result_, "request failed")))
    };
    let _ = result;
}
