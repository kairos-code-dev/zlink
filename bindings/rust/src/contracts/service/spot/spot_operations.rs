use std::any::Any;

/// Typestate marker: no message has been set yet.
pub struct Empty;

/// Typestate marker: at least one message part has been set.
pub struct Ready;

/// Typestate marker: flags have been set, only callback submit available.
pub struct CallbackReady;

pub(crate) trait SendOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub struct SendOp<State> {
    pub(crate) inner: Box<dyn SendOpRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

pub(crate) trait RequestOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub struct RequestOp<State> {
    pub(crate) inner: Box<dyn RequestOpRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

pub(crate) trait ReplyOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub struct ReplyOp<State> {
    pub(crate) inner: Box<dyn ReplyOpRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}
