use std::any::Any;

pub(crate) trait ActorRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// Owned local Actor facade.
pub struct Actor {
    pub(crate) inner: Box<dyn ActorRuntime>,
}
