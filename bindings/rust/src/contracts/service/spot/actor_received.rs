use crate::actor_models::ActorRecvInfo;
use crate::error::{RecvError, RecvResult};
use crate::message::Message;

/// A message received for an actor: its metadata and parts.
///
/// Owns its parts until the envelope is dropped. Reuse one instance across
/// `recv` calls to avoid a per-receive allocation.
pub struct ActorReceived {
    info: Option<ActorRecvInfo>,
    parts: Vec<Message>,
}

impl Default for ActorReceived {
    fn default() -> Self {
        Self::empty()
    }
}

impl ActorReceived {
    /// Creates an empty reusable envelope; reuse it across `recv` calls.
    pub fn empty() -> Self {
        Self {
            info: None,
            parts: Vec::new(),
        }
    }

    pub(crate) fn new(info: ActorRecvInfo, parts: Vec<Message>) -> Self {
        Self {
            info: Some(info),
            parts,
        }
    }

    pub(crate) fn adopt_from(&mut self, other: ActorReceived) {
        self.close_parts();
        self.info = other.info;
        self.parts = other.parts;
    }

    /// Returns the receive metadata; errors when the envelope holds no message.
    pub fn info(&self) -> Result<&ActorRecvInfo, RecvError> {
        self.info
            .as_ref()
            .ok_or_else(|| RecvError::new(RecvResult::NoData, libc::EAGAIN))
    }

    /// Returns the message parts, owned by this envelope.
    pub fn parts(&self) -> &[Message] {
        &self.parts
    }

    /// Returns the first part without transferring ownership; errors when the
    /// envelope has no parts.
    pub fn first_part(&self) -> Result<&Message, RecvError> {
        self.parts
            .first()
            .ok_or_else(|| RecvError::new(RecvResult::NoData, libc::EAGAIN))
    }

    /// Consumes the envelope and returns ownership of all its parts.
    pub fn into_parts(mut self) -> Vec<Message> {
        std::mem::take(&mut self.parts)
    }

    fn close_parts(&mut self) {
        self.parts.clear();
        self.info = None;
    }
}
