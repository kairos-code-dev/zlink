use std::any::Any;

pub(crate) trait SpotRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// Unified SPOT facade over an existing SPOT node.
pub struct Spot {
    pub(crate) inner: Box<dyn SpotRuntime>,
}
