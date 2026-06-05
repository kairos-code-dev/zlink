use super::*;
use crate::runtime_bridge::ActorPublicRuntime;

impl ActorPublicRuntime for Actor {
    fn actor_ref(&self) -> Result<ActorRef, ConfigError> {
        actor_native(self)
            .actor_ref
            .as_ref()
            .cloned()
            .ok_or_else(|| {
                ConfigError::new(crate::error::ConfigResult::InvalidHandle, libc::EINVAL)
            })
    }

    fn close_with_timeout(&mut self, timeout: Duration) -> Result<(), RequestError> {
        let Some(actor_ref) = actor_native_mut(self).actor_ref.take() else {
            return Ok(());
        };
        let node_handle = actor_native(self).node_handle;
        let raw = actor_ref.to_raw().map_err(|err| {
            RequestError::new(
                crate::error::RequestResult::InvalidArgument,
                err.native_errno(),
            )
        })?;
        wait_reply_submit(|handler, userdata| unsafe {
            ffi::zlink_spot_node_actor_destroy(
                node_handle,
                &raw,
                handler,
                userdata,
                timeout_to_timeout_ms(timeout),
            )
        })
    }

    fn close(&mut self) -> Result<(), RequestError> {
        self.close_with_timeout(Duration::from_millis(0))
    }

    /// Async user-Spot join (operation builder).
    fn join(&self, spot: &Spot) -> ActorJoinOp<Empty> {
        let raw_actor =
            self.actor_ref()
                .and_then(|a| a.to_raw())
                .unwrap_or(ffi::zlink_actor_ref_t {
                    node_rid: ffi::zlink_routing_id_t {
                        size: 0,
                        data: [0; 255],
                    },
                    actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
                    generation: 0,
                });
        let dest_node_rid = routing_id_from_handle(spot_node_raw(spot)).unwrap_or_else(|_| {
            RoutingId::from_raw(ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            })
        });
        let dest_spot_rid = spot.routing_id().unwrap_or_else(|_| {
            RoutingId::from_raw(ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            })
        });
        wrap_actor_join_op(NativeActorJoinOp {
            node_handle: actor_native(self).node_handle,
            spot_handle: spot_handle(spot),
            actor: raw_actor,
            dest_node_rid,
            dest_spot_rid,
            parts: Vec::new(),
            flags: SendFlags::NONE,
            timeout: Duration::ZERO,
        })
    }

    /// Async leave to the same node's Entry Spot (operation builder).
    fn leave(&self, spot: &Spot) -> ActorLeaveOp<Empty> {
        let raw_actor =
            self.actor_ref()
                .and_then(|a| a.to_raw())
                .unwrap_or(ffi::zlink_actor_ref_t {
                    node_rid: ffi::zlink_routing_id_t {
                        size: 0,
                        data: [0; 255],
                    },
                    actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
                    generation: 0,
                });
        let dest_spot_rid = spot.routing_id().unwrap_or_else(|_| {
            RoutingId::from_raw(ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            })
        });
        wrap_actor_reply_op(NativeActorReplyOp {
            handle: actor_native(self).node_handle,
            kind: NativeActorReplyOpKind::Leave {
                actor: raw_actor,
                current_spot_rid: dest_spot_rid,
            },
            timeout: Duration::ZERO,
        })
    }

    fn recv(&self, out: &mut ActorReceived, flags: RecvFlags) -> Result<bool, RecvError> {
        let actor_ref = self.actor_ref().map_err(recv_error_from_config)?;
        match recv_actor_once(actor_native(self).node_handle, &actor_ref, flags)? {
            Some(received) => {
                out.adopt_from(received);
                Ok(true)
            }
            None => Ok(false),
        }
    }

    /// Actor-to-session relay (operation builder).
    fn send_bound_session_msg(&self) -> SendOp<Empty> {
        let raw_actor =
            self.actor_ref()
                .and_then(|a| a.to_raw())
                .unwrap_or(ffi::zlink_actor_ref_t {
                    node_rid: ffi::zlink_routing_id_t {
                        size: 0,
                        data: [0; 255],
                    },
                    actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
                    generation: 0,
                });
        wrap_send_op(NativeSendOp {
            handle: actor_native(self).node_handle,
            kind: SendOpKind::ActorBoundSession { actor: raw_actor },
            parts: Vec::new(),
            flags: SendFlags::NONE,
        })
    }

    fn close_bound_session(&self, timeout: Duration) -> Result<(), RequestError> {
        let raw_actor = self
            .actor_ref()
            .map_err(|err| {
                RequestError::new(
                    crate::error::RequestResult::InvalidArgument,
                    err.native_errno(),
                )
            })?
            .to_raw()
            .map_err(|err| {
                RequestError::new(
                    crate::error::RequestResult::InvalidArgument,
                    err.native_errno(),
                )
            })?;
        check_request_result(unsafe {
            ffi::zlink_spot_node_actor_close_bound_session(
                actor_native(self).node_handle,
                &raw_actor,
                timeout_to_timeout_ms(timeout),
            )
        })
    }
}

impl Drop for Actor {
    fn drop(&mut self) {
        let _ = self.close();
    }
}
