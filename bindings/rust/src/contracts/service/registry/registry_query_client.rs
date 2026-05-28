use std::any::Any;

pub(crate) trait RegistryQueryClientRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// Read-only registry query client for remote topology snapshots.
pub struct RegistryQueryClient {
    pub(crate) inner: Box<dyn RegistryQueryClientRuntime>,
}
