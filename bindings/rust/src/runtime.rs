use crate::error::{ConfigError, check_rc};
use crate::ffi;
use crate::message::Message;
use crate::poller::PollTarget;

pub fn proxy<'a>(
    frontend: impl Into<PollTarget<'a>>,
    backend: impl Into<PollTarget<'a>>,
    capture: Option<impl Into<PollTarget<'a>>>,
) -> Result<(), ConfigError> {
    let frontend = frontend.into();
    let backend = backend.into();
    let capture = capture.map(Into::into);
    check_rc(unsafe {
        ffi::zlink_proxy(
            frontend.raw(),
            backend.raw(),
            capture
                .as_ref()
                .map_or(std::ptr::null_mut(), PollTarget::raw),
        )
    })
}

pub fn proxy_steerable<'a>(
    frontend: impl Into<PollTarget<'a>>,
    backend: impl Into<PollTarget<'a>>,
    capture: Option<impl Into<PollTarget<'a>>>,
    control: impl Into<PollTarget<'a>>,
) -> Result<(), ConfigError> {
    let frontend = frontend.into();
    let backend = backend.into();
    let capture = capture.map(Into::into);
    let control = control.into();
    check_rc(unsafe {
        ffi::zlink_proxy_steerable(
            frontend.raw(),
            backend.raw(),
            capture
                .as_ref()
                .map_or(std::ptr::null_mut(), PollTarget::raw),
            control.raw(),
        )
    })
}

pub fn sleep(seconds: i32) {
    unsafe {
        ffi::zlink_sleep(seconds);
    }
}

pub fn multipart_close(parts: &mut [Message]) {
    for part in parts.iter_mut() {
        part.close_now();
    }
}
