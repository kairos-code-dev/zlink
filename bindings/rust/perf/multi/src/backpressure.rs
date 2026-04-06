pub struct SocketBackpressure {
    pending: bool,
    pollout_on: bool,
}

impl SocketBackpressure {
    pub fn new() -> Self {
        Self {
            pending: false,
            pollout_on: false,
        }
    }

    pub fn is_pending(&self) -> bool {
        self.pending
    }

    pub fn mark_pending<F>(&mut self, register_pollout: F)
    where
        F: FnOnce(),
    {
        self.pending = true;
        if !self.pollout_on {
            register_pollout();
            self.pollout_on = true;
        }
    }

    pub fn clear_pending<F>(&mut self, unregister_pollout: F)
    where
        F: FnOnce(),
    {
        self.pending = false;
        if self.pollout_on {
            unregister_pollout();
            self.pollout_on = false;
        }
    }
}
