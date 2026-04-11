use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use crate::domain::{Received, SendResult};
use crate::error::{ZlinkError, check_rc};
use crate::ffi;
use crate::message::{Message, RoutingId};
use crate::socket::{DealerSocket, RouterSocket, prepare_send_parts, take_parts};

const DEFAULT_TIMEOUT: Duration = Duration::from_secs(5);

type DataHandler = Arc<dyn Fn(Received) + Send + Sync + 'static>;

struct ReplyCallbackState {
    tx: mpsc::Sender<Result<Received, ZlinkError>>,
}

struct RouterRequestState {
    tx: mpsc::Sender<Received>,
    handler: Mutex<Option<DataHandler>>,
}

pub struct RequestDealer {
    socket: DealerSocket,
    default_timeout: Mutex<Duration>,
}

pub struct RequestRouter {
    socket: RouterSocket,
    state: Arc<RouterRequestState>,
    data_rx: Mutex<mpsc::Receiver<Received>>,
    default_timeout: Mutex<Duration>,
}

unsafe impl Send for RequestDealer {}
unsafe impl Sync for RequestDealer {}
unsafe impl Send for RequestRouter {}
unsafe impl Sync for RequestRouter {}

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

    pub fn request_callback_with_timeout<F>(&self, msg: Message, callback: F, timeout: Duration)
    where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        let result = self.request_impl(msg, timeout);
        callback(result);
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

    pub fn try_request_callback_with_timeout<F>(&self, msg: Message, callback: F, timeout: Duration)
    where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        let result = self.request_impl(msg, timeout);
        callback(result);
    }

    pub fn recv(&self) -> Result<Received, ZlinkError> {
        self.socket.recv()
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
        let mut parts = vec![msg];
        let mut native = prepare_send_parts(&mut parts)?;
        let (tx, rx) = mpsc::channel();
        let state = Box::new(ReplyCallbackState { tx });
        let state_ptr = Box::into_raw(state);
        let rc = unsafe {
            ffi::zlink_dealer_request(
                self.socket.inner.handle,
                native.as_mut_ptr(),
                native.len(),
                dealer_reply_callback,
                state_ptr.cast(),
                0,
                duration_to_timeout_ms(timeout),
            )
        };
        if rc != 0 {
            unsafe { drop(Box::from_raw(state_ptr)); }
            return Err(ZlinkError::last());
        }
        rx.recv().unwrap_or_else(|_| Err(ZlinkError::state("request callback dropped")))
    }
}

impl RequestRouter {
    pub fn new(socket: RouterSocket) -> Result<Self, ZlinkError> {
        let (tx, rx) = mpsc::channel();
        let state = Arc::new(RouterRequestState {
            tx,
            handler: Mutex::new(None),
        });
        check_rc(unsafe {
            ffi::zlink_router_handler(
                socket.inner.handle,
                router_request_callback,
                Arc::as_ptr(&state) as *mut _,
            )
        })?;
        Ok(Self {
            socket,
            state,
            data_rx: Mutex::new(rx),
            default_timeout: Mutex::new(DEFAULT_TIMEOUT),
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

    pub fn request_callback_with_timeout<F>(
        &self,
        routing_id: RoutingId,
        msg: Message,
        callback: F,
        timeout: Duration,
    ) where
        F: FnOnce(Result<Received, ZlinkError>) + Send + 'static,
    {
        let result = self.request_impl(&routing_id, msg, timeout);
        callback(result);
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
        let result = self.request_impl(&routing_id, msg, timeout);
        callback(result);
    }

    pub fn reply(
        &self,
        routing_id: &RoutingId,
        request_seq: u64,
        msg: Message,
    ) -> Result<(), ZlinkError> {
        let mut parts = vec![msg];
        let mut native = prepare_send_parts(&mut parts)?;
        check_rc(unsafe {
            ffi::zlink_router_reply(
                self.socket.inner.handle,
                routing_id.as_raw(),
                request_seq,
                native.as_mut_ptr(),
                native.len(),
            )
        })
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
        recv_data(&self.state.handler, &self.data_rx)
    }

    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError> {
        try_recv_data(&self.state.handler, &self.data_rx)
    }

    pub fn on_receive<F>(&self, handler: F)
    where
        F: Fn(Received) + Send + Sync + 'static,
    {
        *self.state.handler.lock().unwrap() = Some(Arc::new(handler));
    }

    fn request_impl(
        &self,
        routing_id: &RoutingId,
        msg: Message,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        let mut parts = vec![msg];
        let mut native = prepare_send_parts(&mut parts)?;
        let (tx, rx) = mpsc::channel();
        let state = Box::new(ReplyCallbackState { tx });
        let state_ptr = Box::into_raw(state);
        let rc = unsafe {
            ffi::zlink_router_request(
                self.socket.inner.handle,
                routing_id.as_raw(),
                native.as_mut_ptr(),
                native.len(),
                dealer_reply_callback,
                state_ptr.cast(),
                0,
                duration_to_timeout_ms(timeout),
            )
        };
        if rc != 0 {
            unsafe { drop(Box::from_raw(state_ptr)); }
            return Err(ZlinkError::last());
        }
        rx.recv().unwrap_or_else(|_| Err(ZlinkError::state("request callback dropped")))
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

fn recv_data(
    data_handler: &Mutex<Option<DataHandler>>,
    data_rx: &Mutex<mpsc::Receiver<Received>>,
) -> Result<Received, ZlinkError> {
    if data_handler.lock().unwrap().is_some() {
        return Err(ZlinkError::state("socket is in callback mode"));
    }
    data_rx
        .lock()
        .unwrap()
        .recv()
        .map_err(|_| ZlinkError::state("request handler closed"))
}

fn try_recv_data(
    data_handler: &Mutex<Option<DataHandler>>,
    data_rx: &Mutex<mpsc::Receiver<Received>>,
) -> Result<Option<Received>, ZlinkError> {
    if data_handler.lock().unwrap().is_some() {
        return Err(ZlinkError::state("socket is in callback mode"));
    }
    match data_rx.lock().unwrap().try_recv() {
        Ok(received) => Ok(Some(received)),
        Err(mpsc::TryRecvError::Empty) => Ok(None),
        Err(mpsc::TryRecvError::Disconnected) => Err(ZlinkError::state("request handler closed")),
    }
}

unsafe extern "C" fn dealer_reply_callback(
    errno_: i32,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut std::ffi::c_void,
) {
    let state = unsafe { Box::from_raw(userdata.cast::<ReplyCallbackState>()) };
    let result = if errno_ == 0 {
        let owned = take_parts(parts, part_count);
        state
            .tx
            .send(Ok(Received::new(RoutingId::from_raw(ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            }), owned)))
    } else {
        state
            .tx
            .send(Err(ZlinkError::native(errno_, "request failed")))
    };
    let _ = result;
}

unsafe extern "C" fn router_request_callback(
    peer_rid: *const ffi::zlink_routing_id_t,
    request_seq: u64,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut std::ffi::c_void,
) {
    let state = unsafe { &*(userdata as *const RouterRequestState) };
    let routing_id = unsafe { RoutingId::from_raw(*peer_rid) };
    let received = Received::with_request_seq(routing_id, take_parts(parts, part_count), request_seq);
    if let Some(handler) = state.handler.lock().unwrap().as_ref().cloned() {
        handler(received);
    } else {
        let _ = state.tx.send(received);
    }
}
