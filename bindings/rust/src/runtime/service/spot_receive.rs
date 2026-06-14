// SPDX-License-Identifier: MPL-2.0

use std::ffi::c_void;
use std::mem::MaybeUninit;
use std::ptr;

use crate::error::{RecvError, RecvResult};
use crate::ffi;
use crate::message::{Message, RoutingId};
use crate::native_errors::check_recv_rc;
use crate::socket::{close_unreceived_part, routing_id_from_ptr};

pub(super) type SpotRoutedParts = Result<
    Option<(
        *const ffi::zlink_routing_id_t,
        *const ffi::zlink_routing_id_t,
        u64,
        Vec<Message>,
    )>,
    RecvError,
>;

pub(super) type SpotSubscribedParts =
    Result<Option<(Option<RoutingId>, smol_str::SmolStr, Vec<Message>)>, RecvError>;

pub(super) fn recv_spot_routed_parts(
    handle: *mut c_void,
    flags: ffi::zlink_recv_flags_t,
) -> SpotRoutedParts {
    let mut source_node_rid_ptr: *const ffi::zlink_routing_id_t = ptr::null();
    let mut source_spot_rid_ptr: *const ffi::zlink_routing_id_t = ptr::null();
    let mut request_seq: u64 = 0;
    let mut parts = Vec::new();
    let mut recv_flags = flags;

    loop {
        let mut part = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(part.as_mut_ptr());
        }
        let mut has_more = 0;
        let rc = unsafe {
            ffi::zlink_spot_recv_part(
                handle,
                &mut source_node_rid_ptr,
                &mut source_spot_rid_ptr,
                &mut request_seq,
                part.as_mut_ptr(),
                &mut has_more,
                recv_flags,
            )
        };

        if parts.is_empty() {
            if rc == RecvResult::NoData as i32 {
                close_unreceived_part(&mut part);
                return Ok(None);
            }
            if rc != 0 {
                close_unreceived_part(&mut part);
                let errno = unsafe { ffi::zlink_errno() };
                if errno == libc::EAGAIN {
                    return Ok(None);
                }
                return Err(check_recv_rc(rc).unwrap_err());
            }
        } else if rc != 0 {
            close_unreceived_part(&mut part);
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { Message::from_raw(part.assume_init()) });
        if has_more == 0 {
            return Ok(Some((
                source_node_rid_ptr,
                source_spot_rid_ptr,
                request_seq,
                parts,
            )));
        }
        recv_flags = ffi::ZLINK_DONTWAIT;
    }
}

pub(super) fn recv_spot_subscribed_parts(
    handle: *mut c_void,
    topic_buf: &mut [i8; 256],
    flags: ffi::zlink_recv_flags_t,
) -> SpotSubscribedParts {
    let mut routing_id = None;
    let mut topic = smol_str::SmolStr::default();
    let mut parts = Vec::new();
    let mut recv_flags = flags;

    loop {
        let mut source_rid_ptr = ptr::null();
        let mut topic_len = topic_buf.len();
        let mut part = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(part.as_mut_ptr());
        }
        let mut has_more = 0;
        let rc = unsafe {
            ffi::zlink_spot_subscribe_part(
                handle,
                &mut source_rid_ptr,
                topic_buf.as_mut_ptr(),
                topic_buf.len(),
                &mut topic_len,
                part.as_mut_ptr(),
                &mut has_more,
                recv_flags,
            )
        };

        if parts.is_empty() {
            if rc == RecvResult::NoData as i32 {
                close_unreceived_part(&mut part);
                return Ok(None);
            }
            if rc != 0 {
                close_unreceived_part(&mut part);
                let errno = unsafe { ffi::zlink_errno() };
                if errno == libc::EAGAIN {
                    return Ok(None);
                }
                return Err(check_recv_rc(rc).unwrap_err());
            }
            routing_id = routing_id_from_ptr(source_rid_ptr);
            topic = crate::socket::cstr_buf_to_smolstr(topic_buf, topic_len);
        } else if rc != 0 {
            close_unreceived_part(&mut part);
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { Message::from_raw(part.assume_init()) });
        if has_more == 0 {
            return Ok(Some((routing_id, topic, parts)));
        }
        recv_flags = ffi::ZLINK_DONTWAIT;
    }
}
