mod dealer;
mod macros;
mod pair;
mod pub_socket;
mod router;
mod stream;
mod sub;
mod xpub;
mod xsub;

pub(crate) use dealer::dealer_inner;
pub(crate) use macros::{
    impl_attach_discovery, impl_base_socket, impl_connect, impl_routing_id_options,
};
pub(crate) use pair::pair_handle;
pub(crate) use pub_socket::pub_inner;
pub(crate) use router::router_inner;
pub(crate) use stream::stream_inner;
pub(crate) use sub::sub_inner;
pub(crate) use xpub::xpub_inner;
pub(crate) use xsub::xsub_inner;

use std::ffi::{CStr, CString, c_void};
use std::mem::MaybeUninit;
use std::ptr;
use std::time::Duration;

use crate::ctx::{context_handle, duration_to_millis};
use crate::discovery_resource::Discovery;
use crate::domain::Received;
use crate::error::{
    BindError, CloseError, ConfigError, ConnectError, HandlerError, RecvError, RecvResult,
    SubmitError,
};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::message::{Message, RoutingId};
use crate::messaging_subscription_event::SubscriptionEvent;
use crate::native_errors::{
    check_bind_rc, check_close_rc, check_config_rc, check_connect_rc, check_handler_rc,
    check_recv_rc, config_validation_error, last_errno, submit_validation_error,
};
use crate::topic_message_contract::TopicMessage;

mod socket_inner_runtime;
pub(crate) use socket_inner_runtime::*;
mod socket_parts_runtime;
pub(crate) use socket_parts_runtime::*;
mod socket_options_runtime;
use socket_options_runtime::*;
