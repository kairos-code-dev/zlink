use std::any::Any;

pub(crate) trait SpotNodeRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// SPOT node runtime facade for topology, discovery, and lifecycle.
pub struct SpotNode {
    pub(crate) inner: Box<dyn SpotNodeRuntime>,
}
