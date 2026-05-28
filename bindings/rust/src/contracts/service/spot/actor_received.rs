use crate::actor_models::ActorRecvInfo;
use crate::error::{RecvError, RecvResult};
use crate::message::Message;

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

    pub fn info(&self) -> Result<&ActorRecvInfo, RecvError> {
        self.info
            .as_ref()
            .ok_or_else(|| RecvError::new(RecvResult::NoData, libc::EAGAIN))
    }

    pub fn parts(&self) -> &[Message] {
        &self.parts
    }

    pub fn first_part(&self) -> Result<&Message, RecvError> {
        self.parts
            .first()
            .ok_or_else(|| RecvError::new(RecvResult::NoData, libc::EAGAIN))
    }

    pub fn into_parts(mut self) -> Vec<Message> {
        std::mem::take(&mut self.parts)
    }

    fn close_parts(&mut self) {
        self.parts.clear();
        self.info = None;
    }
}
