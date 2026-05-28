#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SendResult {
    Sent,
    Backpressured,
    NotReady,
}

impl SendResult {
    pub fn is_sent(&self) -> bool {
        matches!(self, Self::Sent)
    }
}
