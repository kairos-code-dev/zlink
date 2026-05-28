use std::any::Any;

pub(crate) trait RegistryRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// Service registry that accepts registrations and broadcasts the service list.
pub struct Registry {
    pub(crate) inner: Box<dyn RegistryRuntime>,
}
