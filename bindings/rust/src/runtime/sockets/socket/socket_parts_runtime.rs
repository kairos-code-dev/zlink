use super::*;

pub(crate) const MAX_NATIVE_PARTS: usize = 1024;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Prepare a contiguous native `zlink_msg_t` array for sending.
///
/// Uses `zlink_msg_move` to properly transfer each Message's content into a
/// freshly-initialized contiguous buffer. This avoids memcpy-based relocation
/// of initialized `zlink_msg_t` structs, which can invalidate internal state.
/// After this call, the source Messages are empty (moved-from) and the returned
/// Vec owns the content ready for `zlink_send`.
pub(crate) fn prepare_send_parts(
    parts: &mut [Message],
) -> Result<Vec<ffi::zlink_msg_t>, SubmitError> {
    let mut native: Vec<ffi::zlink_msg_t> = Vec::with_capacity(parts.len());
    unsafe {
        for part in parts.iter_mut() {
            let mut dest = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            ffi::zlink_msg_init(dest.as_mut_ptr());
            ffi::zlink_msg_move(dest.as_mut_ptr(), part.raw_mut());
            native.push(dest.assume_init());
        }
    }
    Ok(native)
}

pub(crate) fn submit_part_sequence(
    native: &mut [ffi::zlink_msg_t],
    mut submit: impl FnMut(*mut ffi::zlink_msg_t, ffi::zlink_part_flag_t, bool) -> i32,
) -> Result<i32, SubmitError> {
    if native.is_empty() {
        return Err(submit_validation_error());
    }

    for index in 0..native.len() {
        let is_final = index + 1 == native.len();
        let part_flag = if is_final {
            ffi::zlink_part_flag_t::ZLINK_PART_FINAL
        } else {
            ffi::zlink_part_flag_t::ZLINK_PART_MORE
        };
        let rc = submit(native.as_mut_ptr().wrapping_add(index), part_flag, is_final);
        if rc != 0 {
            close_native_parts_from(native, index + 1);
            return Ok(rc);
        }
    }

    Ok(0)
}

fn close_native_parts_from(parts: &mut [ffi::zlink_msg_t], start_index: usize) {
    for part in &mut parts[start_index..] {
        unsafe {
            ffi::zlink_msg_close(part);
        }
    }
}

pub(crate) fn close_unreceived_part(part: &mut MaybeUninit<ffi::zlink_msg_t>) {
    unsafe {
        ffi::zlink_msg_close(part.as_mut_ptr());
    }
}

/// Take ownership of `part_count` messages from a native-owned array.
pub(crate) fn take_parts(parts_ptr: *mut ffi::zlink_msg_t, part_count: usize) -> Vec<Message> {
    if parts_ptr.is_null() || part_count == 0 {
        return Vec::new();
    }
    let count = part_count.min(MAX_NATIVE_PARTS);
    let mut out = Vec::with_capacity(count);
    for i in 0..count {
        unsafe {
            // Move content from the library-owned array into our own zlink_msg_t.
            // ptr::read would create a bitwise copy sharing the same internal
            // storage, leading to double-free on drop. zlink_msg_move properly
            // transfers ownership and leaves the source as an empty message.
            let mut dest = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            ffi::zlink_msg_init(dest.as_mut_ptr());
            ffi::zlink_msg_move(dest.as_mut_ptr(), parts_ptr.add(i));
            out.push(Message::from_raw(dest.assume_init()));
        }
    }
    unsafe {
        ffi::zlink_multipart_close(parts_ptr, part_count);
    }
    out
}

// Short subscribe topics bypass heap allocation entirely (<=22 bytes live
// inline).
pub(crate) fn cstr_buf_to_smolstr(buf: &[i8], len: usize) -> smol_str::SmolStr {
    let bytes: &[u8] =
        unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len.min(buf.len())) };
    match std::str::from_utf8(bytes) {
        Ok(s) => smol_str::SmolStr::new(s),
        Err(_) => smol_str::SmolStr::new(String::from_utf8_lossy(bytes)),
    }
}

pub(crate) fn routing_id_from_ptr(raw: *const ffi::zlink_routing_id_t) -> Option<RoutingId> {
    if raw.is_null() {
        None
    } else {
        RoutingId::from_raw_optional(unsafe { *raw })
    }
}

type RecvBasicParts = Result<Option<(Option<RoutingId>, Vec<Message>)>, RecvError>;
type RecvSubscribedParts =
    Result<Option<(Option<RoutingId>, smol_str::SmolStr, Vec<Message>)>, RecvError>;

pub(crate) fn recv_basic_parts(
    handle: *mut c_void,
    flags: ffi::zlink_recv_flags_t,
) -> RecvBasicParts {
    let mut routing_id = None;
    let mut parts = Vec::new();
    let mut recv_flags = flags;

    loop {
        let mut source_rid_ptr = ptr::null();
        let mut part = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(part.as_mut_ptr());
        }
        let mut has_more = ffi::zlink_part_flag_t::ZLINK_PART_FINAL;
        let rc = unsafe {
            ffi::zlink_recv_part(
                handle,
                &mut source_rid_ptr,
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
        } else if rc != 0 {
            close_unreceived_part(&mut part);
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { Message::from_raw(part.assume_init()) });
        if has_more == ffi::zlink_part_flag_t::ZLINK_PART_FINAL {
            return Ok(Some((routing_id, parts)));
        }
        recv_flags = ffi::ZLINK_DONTWAIT;
    }
}

pub(crate) fn recv_subscribed_parts(
    handle: *mut c_void,
    topic_buf: &mut [i8; 256],
    flags: ffi::zlink_recv_flags_t,
) -> RecvSubscribedParts {
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
        let mut has_more = ffi::zlink_part_flag_t::ZLINK_PART_FINAL;
        let rc = unsafe {
            ffi::zlink_subscribe_part(
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
            topic = cstr_buf_to_smolstr(topic_buf, topic_len);
        } else if rc != 0 {
            close_unreceived_part(&mut part);
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { Message::from_raw(part.assume_init()) });
        if has_more == ffi::zlink_part_flag_t::ZLINK_PART_FINAL {
            return Ok(Some((routing_id, topic, parts)));
        }
        recv_flags = ffi::ZLINK_DONTWAIT;
    }
}
