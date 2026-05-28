use std::any::Any;

pub(crate) trait DiscoveryRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// Discovery instance with a fixed service view.
pub struct Discovery {
    pub(crate) inner: Box<dyn DiscoveryRuntime>,
}
