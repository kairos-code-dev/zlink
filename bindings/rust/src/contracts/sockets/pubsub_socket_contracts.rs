// SPDX-License-Identifier: MPL-2.0

use crate::socket_contracts::SocketRuntime;
use crate::{
    BindError, CommonSocketOptions, ConfigError, ConnectError, Discovery, HandlerError,
    PubSocketOptions, RecvError, RecvFlags, SubSocketOptions, SubscriptionEvent, TopicMessage,
};
use crate::{Empty, SendOp};

macro_rules! define_pubsub_socket {
    ($name:ident, $doc:literal) => {
        #[doc = $doc]
        pub struct $name {
            pub(crate) inner: Box<dyn SocketRuntime>,
        }

        impl std::panic::UnwindSafe for $name {}
        impl std::panic::RefUnwindSafe for $name {}
    };
}

define_pubsub_socket!(PubSocket, "PUB socket, the topic publishing socket.");
define_pubsub_socket!(SubSocket, "SUB socket, the topic subscriber socket.");
define_pubsub_socket!(
    XPubSocket,
    "XPUB socket, the extended publisher socket with subscription events."
);
define_pubsub_socket!(XSubSocket, "XSUB socket, the extended subscriber socket.");

macro_rules! impl_pubsub_common {
    ($ty:ident, $inner:path, $inner_mut:path) => {
        impl $ty {
            pub fn close(&mut self) -> Result<(), crate::error::CloseError> {
                $inner_mut(self).close()
            }

            pub fn bind(&self, addr: &str) -> Result<(), BindError> {
                $inner(self).bind(addr)
            }

            pub fn unbind(&self, addr: &str) -> Result<(), ConnectError> {
                $inner(self).unbind(addr)
            }

            pub fn last_endpoint(&self) -> Result<String, ConfigError> {
                $inner(self).last_endpoint()
            }

            pub fn set_tls_cert(&self, cert: &str) -> Result<(), ConfigError> {
                $inner(self).set_tls_cert(cert)
            }

            pub fn set_tls_key(&self, key: &str) -> Result<(), ConfigError> {
                $inner(self).set_tls_key(key)
            }

            pub fn set_tls_ca(&self, ca_cert: &str) -> Result<(), ConfigError> {
                $inner(self).set_tls_ca(ca_cert)
            }

            pub fn set_tls_hostname(&self, hostname: &str) -> Result<(), ConfigError> {
                $inner(self).set_tls_hostname(hostname)
            }

            pub fn set_tls_trust_system(&self, trust_system: bool) -> Result<(), ConfigError> {
                $inner(self).set_tls_trust_system(trust_system)
            }

            pub fn set_tls_server(
                &self,
                cert: &str,
                key: &str,
                require_client_cert: bool,
            ) -> Result<(), ConfigError> {
                $inner(self).set_tls_server(cert, key, require_client_cert)
            }

            pub fn set_tls_client(
                &self,
                ca_cert: &str,
                hostname: &str,
                trust_system: bool,
            ) -> Result<(), ConfigError> {
                $inner(self).set_tls_client(ca_cert, hostname, trust_system)
            }

            pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError> {
                $inner(self).attach_discovery(discovery)
            }

            pub fn connect(&self, addr: &str) -> Result<(), ConnectError> {
                $inner(self).connect(addr)
            }

            pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError> {
                $inner(self).disconnect(addr)
            }

            pub fn disconnect_rid(
                &self,
                peer_rid: &crate::message::RoutingId,
            ) -> Result<(), ConnectError> {
                $inner(self).disconnect_rid(peer_rid)
            }
        }
    };
}

impl PubSocket {
    pub fn publish(&self, topic: &str) -> SendOp<Empty> {
        let topic = crate::service::fixed_cstring_or_panic(topic, "topic");
        crate::service::socket_publish_op(crate::socket::pub_inner(self).handle, topic)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        crate::socket::pub_inner_mut(self).on_send_ready(handler)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(crate::socket::pub_inner(self))
    }

    pub fn pub_options(&self) -> PubSocketOptions<'_> {
        PubSocketOptions::new(crate::socket::pub_inner(self))
    }
}

impl_pubsub_common!(
    PubSocket,
    crate::socket::pub_inner,
    crate::socket::pub_inner_mut
);

impl SubSocket {
    pub fn subscribe(&self, out: &mut TopicMessage, flags: RecvFlags) -> Result<bool, RecvError> {
        crate::socket::sub_inner(self).subscribe_recv(out, flags)
    }

    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        crate::socket::sub_inner(self).set_subscription(filter)
    }

    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        crate::socket::sub_inner(self).unset_subscription(filter)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(crate::socket::sub_inner(self))
    }

    pub fn sub_options(&self) -> SubSocketOptions<'_> {
        SubSocketOptions::new(crate::socket::sub_inner(self))
    }
}

impl_pubsub_common!(
    SubSocket,
    crate::socket::sub_inner,
    crate::socket::sub_inner_mut
);

impl XPubSocket {
    pub fn publish(&self, topic: &str) -> SendOp<Empty> {
        let topic = crate::service::fixed_cstring_or_panic(topic, "topic");
        crate::service::socket_publish_op(crate::socket::xpub_inner(self).handle, topic)
    }

    pub fn receive_subscription_event(
        &self,
        out: &mut SubscriptionEvent,
        flags: RecvFlags,
    ) -> Result<bool, RecvError> {
        crate::socket::xpub_inner(self).receive_subscription_event(out, flags)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        crate::socket::xpub_inner_mut(self).on_send_ready(handler)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(crate::socket::xpub_inner(self))
    }

    pub fn pub_options(&self) -> PubSocketOptions<'_> {
        PubSocketOptions::new(crate::socket::xpub_inner(self))
    }
}

impl_pubsub_common!(
    XPubSocket,
    crate::socket::xpub_inner,
    crate::socket::xpub_inner_mut
);

impl XSubSocket {
    pub fn subscribe(&self, out: &mut TopicMessage, flags: RecvFlags) -> Result<bool, RecvError> {
        crate::socket::xsub_inner(self).subscribe_recv(out, flags)
    }

    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        crate::socket::xsub_inner(self).set_subscription(filter)
    }

    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        crate::socket::xsub_inner(self).unset_subscription(filter)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(crate::socket::xsub_inner(self))
    }

    pub fn sub_options(&self) -> SubSocketOptions<'_> {
        SubSocketOptions::new(crate::socket::xsub_inner(self))
    }
}

impl_pubsub_common!(
    XSubSocket,
    crate::socket::xsub_inner,
    crate::socket::xsub_inner_mut
);
