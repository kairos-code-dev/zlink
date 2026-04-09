use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, mpsc};
use std::time::Duration;

use crate::domain::{Received, SendResult};
use crate::error::ZlinkError;
use crate::message::{Message, RoutingId};
use crate::{DealerSocket, RouterSocket};

const DEFAULT_TIMEOUT: Duration = Duration::from_secs(30);
const ETERM_NATIVE: i32 = 156_384_765;

type DataHandler = Arc<dyn Fn(Received) + Send + Sync + 'static>;
struct DealerState {
    pending: Mutex<HashMap<u64, mpsc::Sender<Result<Received, ZlinkError>>>>,
    data_tx: mpsc::Sender<Received>,
    data_handler: Mutex<Option<DataHandler>>,
}

struct RouterState {
    pending: Mutex<HashMap<u64, mpsc::Sender<Result<Received, ZlinkError>>>>,
    data_tx: mpsc::Sender<Received>,
    data_handler: Mutex<Option<DataHandler>>,
}

type PendingMap = Mutex<HashMap<u64, mpsc::Sender<Result<Received, ZlinkError>>>>;

pub struct RequestDealer {
    socket: Arc<Mutex<DealerSocket>>,
    state: Arc<DealerState>,
    data_rx: Mutex<mpsc::Receiver<Received>>,
    next_correlation_id: AtomicU64,
    default_timeout: Mutex<Duration>,
    dispatch_stop: Arc<std::sync::atomic::AtomicBool>,
}

pub struct RequestRouter {
    socket: Arc<Mutex<RouterSocket>>,
    state: Arc<RouterState>,
    data_rx: Mutex<mpsc::Receiver<Received>>,
    next_correlation_id: AtomicU64,
    default_timeout: Mutex<Duration>,
    dispatch_stop: Arc<std::sync::atomic::AtomicBool>,
}

impl RequestDealer {
    pub fn new(socket: DealerSocket) -> Result<Self, ZlinkError> {
        let (data_tx, data_rx) = mpsc::channel();
        let state = Arc::new(DealerState {
            pending: Mutex::new(HashMap::new()),
            data_tx,
            data_handler: Mutex::new(None),
        });
        let socket = Arc::new(Mutex::new(socket));
        let dispatch_stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
        spawn_dealer_dispatch_thread(
            Arc::clone(&socket),
            Arc::clone(&state),
            Arc::clone(&dispatch_stop),
        );

        Ok(Self {
            socket,
            state,
            data_rx: Mutex::new(data_rx),
            next_correlation_id: AtomicU64::new(1),
            default_timeout: Mutex::new(DEFAULT_TIMEOUT),
            dispatch_stop,
        })
    }

    pub fn set_default_request_timeout(&self, timeout: Duration) {
        *self.default_timeout.lock().unwrap() = timeout;
    }

    pub fn get_default_request_timeout(&self) -> Duration {
        *self.default_timeout.lock().unwrap()
    }

    pub async fn request(&self, msg: Message) -> Result<Received, ZlinkError> {
        self.request_with_timeout(msg, self.get_default_request_timeout()).await
    }

    pub async fn request_with_timeout(
        &self,
        mut msg: Message,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        let correlation_id = self.next_correlation_id.fetch_add(1, Ordering::Relaxed);
        msg.set_request(correlation_id)?;
        let rx = register_pending(&self.state.pending, correlation_id);
        if let Err(err) = self.socket.lock().unwrap().send(vec![msg]) {
            self.state.pending.lock().unwrap().remove(&correlation_id);
            return Err(err);
        }
        await_pending_reply(&self.state.pending, correlation_id, rx, timeout)
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
        let socket = Arc::clone(&self.socket);
        let state = Arc::clone(&self.state);
        let correlation_id = self.next_correlation_id.fetch_add(1, Ordering::Relaxed);
        std::thread::spawn(move || {
            let result = (|| {
                let mut msg = msg;
                msg.set_request(correlation_id)?;
                let rx = register_pending(&state.pending, correlation_id);
                if let Err(err) = socket.lock().unwrap().send(vec![msg]) {
                    state.pending.lock().unwrap().remove(&correlation_id);
                    return Err(err);
                }
                await_pending_reply(&state.pending, correlation_id, rx, timeout)
            })();
            callback(result);
        });
    }

    pub async fn try_request(&self, msg: Message) -> Result<Received, ZlinkError> {
        self.try_request_with_timeout(msg, self.get_default_request_timeout())
            .await
    }

    pub async fn try_request_with_timeout(
        &self,
        mut msg: Message,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        let correlation_id = self.next_correlation_id.fetch_add(1, Ordering::Relaxed);
        msg.set_request(correlation_id)?;
        let rx = register_pending(&self.state.pending, correlation_id);
        match self.socket.lock().unwrap().try_send(vec![msg])? {
            SendResult::Sent => {}
            other => {
                self.state.pending.lock().unwrap().remove(&correlation_id);
                return Err(send_result_error(other));
            }
        }
        await_pending_reply(&self.state.pending, correlation_id, rx, timeout)
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
        let socket = Arc::clone(&self.socket);
        let state = Arc::clone(&self.state);
        let correlation_id = self.next_correlation_id.fetch_add(1, Ordering::Relaxed);
        std::thread::spawn(move || {
            let result = (|| {
                let mut msg = msg;
                msg.set_request(correlation_id)?;
                let rx = register_pending(&state.pending, correlation_id);
                match socket.lock().unwrap().try_send(vec![msg])? {
                    SendResult::Sent => {}
                    other => {
                        state.pending.lock().unwrap().remove(&correlation_id);
                        return Err(send_result_error(other));
                    }
                }
                await_pending_reply(&state.pending, correlation_id, rx, timeout)
            })();
            callback(result);
        });
    }

    pub fn recv(&self) -> Result<Received, ZlinkError> {
        recv_data(&self.state.data_handler, &self.data_rx)
    }

    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError> {
        try_recv_data(&self.state.data_handler, &self.data_rx)
    }

    pub fn on_receive<F>(&self, handler: F)
    where
        F: Fn(Received) + Send + Sync + 'static,
    {
        *self.state.data_handler.lock().unwrap() = Some(Arc::new(handler));
    }
}

impl RequestRouter {
    pub fn new(socket: RouterSocket) -> Result<Self, ZlinkError> {
        let (data_tx, data_rx) = mpsc::channel();
        let state = Arc::new(RouterState {
            pending: Mutex::new(HashMap::new()),
            data_tx,
            data_handler: Mutex::new(None),
        });
        let socket = Arc::new(Mutex::new(socket));
        let dispatch_stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
        spawn_router_dispatch_thread(
            Arc::clone(&socket),
            Arc::clone(&state),
            Arc::clone(&dispatch_stop),
        );

        Ok(Self {
            socket,
            state,
            data_rx: Mutex::new(data_rx),
            next_correlation_id: AtomicU64::new(1),
            default_timeout: Mutex::new(DEFAULT_TIMEOUT),
            dispatch_stop,
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
        mut msg: Message,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        let correlation_id = self.next_correlation_id.fetch_add(1, Ordering::Relaxed);
        msg.set_request(correlation_id)?;
        let rx = register_pending(&self.state.pending, correlation_id);
        if let Err(err) = self.socket.lock().unwrap().send(routing_id, vec![msg]) {
            self.state.pending.lock().unwrap().remove(&correlation_id);
            return Err(err);
        }
        await_pending_reply(&self.state.pending, correlation_id, rx, timeout)
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
        mut msg: Message,
        timeout: Duration,
    ) -> Result<Received, ZlinkError> {
        let correlation_id = self.next_correlation_id.fetch_add(1, Ordering::Relaxed);
        msg.set_request(correlation_id)?;
        let rx = register_pending(&self.state.pending, correlation_id);
        match self.socket.lock().unwrap().try_send(routing_id, vec![msg])? {
            SendResult::Sent => {}
            other => {
                self.state.pending.lock().unwrap().remove(&correlation_id);
                return Err(send_result_error(other));
            }
        }
        await_pending_reply(&self.state.pending, correlation_id, rx, timeout)
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
        let socket = Arc::clone(&self.socket);
        let state = Arc::clone(&self.state);
        let correlation_id = self.next_correlation_id.fetch_add(1, Ordering::Relaxed);
        std::thread::spawn(move || {
            let result = (|| {
                let mut msg = msg;
                msg.set_request(correlation_id)?;
                let rx = register_pending(&state.pending, correlation_id);
                if let Err(err) = socket.lock().unwrap().send(&routing_id, vec![msg]) {
                    state.pending.lock().unwrap().remove(&correlation_id);
                    return Err(err);
                }
                await_pending_reply(&state.pending, correlation_id, rx, timeout)
            })();
            callback(result);
        });
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
        let socket = Arc::clone(&self.socket);
        let state = Arc::clone(&self.state);
        let correlation_id = self.next_correlation_id.fetch_add(1, Ordering::Relaxed);
        std::thread::spawn(move || {
            let result = (|| {
                let mut msg = msg;
                msg.set_request(correlation_id)?;
                let rx = register_pending(&state.pending, correlation_id);
                match socket.lock().unwrap().try_send(&routing_id, vec![msg])? {
                    SendResult::Sent => {}
                    other => {
                        state.pending.lock().unwrap().remove(&correlation_id);
                        return Err(send_result_error(other));
                    }
                }
                await_pending_reply(&state.pending, correlation_id, rx, timeout)
            })();
            callback(result);
        });
    }

    pub fn reply(
        &self,
        routing_id: &RoutingId,
        correlation_id: u64,
        mut msg: Message,
    ) -> Result<(), ZlinkError> {
        msg.set_reply(correlation_id)?;
        self.socket.lock().unwrap().send(routing_id, vec![msg])
    }

    pub fn try_reply(
        &self,
        routing_id: &RoutingId,
        correlation_id: u64,
        mut msg: Message,
    ) -> Result<SendResult, ZlinkError> {
        msg.set_reply(correlation_id)?;
        self.socket.lock().unwrap().try_send(routing_id, vec![msg])
    }

    pub fn recv(&self) -> Result<Received, ZlinkError> {
        recv_data(&self.state.data_handler, &self.data_rx)
    }

    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError> {
        try_recv_data(&self.state.data_handler, &self.data_rx)
    }

    pub fn on_receive<F>(&self, handler: F)
    where
        F: Fn(Received) + Send + Sync + 'static,
    {
        *self.state.data_handler.lock().unwrap() = Some(Arc::new(handler));
    }
}

fn deliver_dealer_data(state: &DealerState, received: Received) {
    if let Some(handler) = state.data_handler.lock().unwrap().as_ref().cloned() {
        handler(received);
        return;
    }
    let _ = state.data_tx.send(received);
}

fn deliver_router_data(state: &RouterState, received: Received) {
    if let Some(handler) = state.data_handler.lock().unwrap().as_ref().cloned() {
        handler(received);
        return;
    }
    let _ = state.data_tx.send(received);
}

fn register_pending(pending: &PendingMap, correlation_id: u64) -> mpsc::Receiver<Result<Received, ZlinkError>> {
    let (tx, rx) = mpsc::channel();
    pending.lock().unwrap().insert(correlation_id, tx);
    rx
}

fn await_pending_reply(
    pending: &PendingMap,
    correlation_id: u64,
    rx: mpsc::Receiver<Result<Received, ZlinkError>>,
    timeout: Duration,
) -> Result<Received, ZlinkError> {
    match rx.recv_timeout(timeout) {
        Ok(result) => result,
        Err(mpsc::RecvTimeoutError::Timeout) => {
            pending.lock().unwrap().remove(&correlation_id);
            Err(ZlinkError::native(libc::ETIMEDOUT, "request timed out"))
        }
        Err(mpsc::RecvTimeoutError::Disconnected) => Err(ZlinkError::native(
            ETERM_NATIVE,
            "request reply dispatch closed",
        )),
    }
}

fn send_result_error(result: SendResult) -> ZlinkError {
    match result {
        SendResult::Sent => ZlinkError::native(libc::EINVAL, "invalid send result"),
        SendResult::Backpressured => {
            ZlinkError::native(libc::EAGAIN, "request send is backpressured")
        }
        SendResult::NotReady => ZlinkError::native(libc::EAGAIN, "request send is not ready"),
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
        .map_err(|_| ZlinkError::native(ETERM_NATIVE, "request reply dispatch closed"))
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
        Err(mpsc::TryRecvError::Disconnected) => {
            Err(ZlinkError::native(ETERM_NATIVE, "request reply dispatch closed"))
        }
    }
}

fn fail_all_dealer_pending(state: &DealerState, err: &ZlinkError) {
    let pending = std::mem::take(&mut *state.pending.lock().unwrap());
    for (_, reply_tx) in pending {
        let _ = reply_tx.send(Err(ZlinkError::native(err.code(), err.to_string())));
    }
}

fn fail_all_router_pending(state: &RouterState, err: &ZlinkError) {
    let pending = std::mem::take(&mut *state.pending.lock().unwrap());
    for (_, reply_tx) in pending {
        let _ = reply_tx.send(Err(ZlinkError::native(err.code(), err.to_string())));
    }
}

fn spawn_dealer_dispatch_thread(
    socket: Arc<Mutex<DealerSocket>>,
    state: Arc<DealerState>,
    stop: Arc<std::sync::atomic::AtomicBool>,
) {
    std::thread::spawn(move || {
        while !stop.load(Ordering::SeqCst) {
            let maybe_received = match socket.lock().unwrap().try_recv() {
                Ok(maybe_received) => maybe_received,
                Err(err) => {
                    fail_all_dealer_pending(&state, &err);
                    stop.store(true, Ordering::SeqCst);
                    return;
                }
            };
            let Some(received) = maybe_received else {
                std::thread::sleep(Duration::from_millis(1));
                continue;
            };

            let first = match received.parts().first() {
                Some(part) => part,
                None => {
                    deliver_dealer_data(&state, received);
                    continue;
                }
            };
            let (msg_type, correlation_id) = match first.request_info() {
                Ok(info) => info,
                Err(_) => {
                    deliver_dealer_data(&state, received);
                    continue;
                }
            };
            if msg_type == 2 {
                let pending = state.pending.lock().unwrap().remove(&correlation_id);
                if let Some(reply_tx) = pending {
                    let _ = reply_tx.send(Ok(received));
                    continue;
                }
            }
            deliver_dealer_data(&state, received);
        }
    });
}

fn spawn_router_dispatch_thread(
    socket: Arc<Mutex<RouterSocket>>,
    state: Arc<RouterState>,
    stop: Arc<std::sync::atomic::AtomicBool>,
) {
    std::thread::spawn(move || {
        while !stop.load(Ordering::SeqCst) {
            let maybe_received = match socket.lock().unwrap().try_recv() {
                Ok(maybe_received) => maybe_received,
                Err(err) => {
                    fail_all_router_pending(&state, &err);
                    stop.store(true, Ordering::SeqCst);
                    return;
                }
            };
            let Some(received) = maybe_received else {
                std::thread::sleep(Duration::from_millis(1));
                continue;
            };

            let first = match received.parts().first() {
                Some(part) => part,
                None => {
                    deliver_router_data(&state, received);
                    continue;
                }
            };
            let (msg_type, correlation_id) = match first.request_info() {
                Ok(info) => info,
                Err(_) => {
                    deliver_router_data(&state, received);
                    continue;
                }
            };
            if msg_type == 2 {
                let pending = state.pending.lock().unwrap().remove(&correlation_id);
                if let Some(reply_tx) = pending {
                    let _ = reply_tx.send(Ok(received));
                    continue;
                }
            }
            deliver_router_data(&state, received);
        }
    });
}

impl Drop for RequestDealer {
    fn drop(&mut self) {
        self.dispatch_stop.store(true, Ordering::SeqCst);
    }
}

impl Drop for RequestRouter {
    fn drop(&mut self) {
        self.dispatch_stop.store(true, Ordering::SeqCst);
    }
}
