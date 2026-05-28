use crate::routing_id::RoutingId;

pub struct SubscriptionEvent {
    pub routing_id: Option<RoutingId>,
    pub topic: smol_str::SmolStr,
    pub subscribed: bool,
}

impl SubscriptionEvent {
    pub fn empty() -> Self {
        Self {
            routing_id: None,
            topic: smol_str::SmolStr::default(),
            subscribed: false,
        }
    }

    pub(crate) fn new(
        routing_id: Option<RoutingId>,
        subscribed: bool,
        topic: smol_str::SmolStr,
    ) -> Self {
        Self {
            routing_id,
            topic,
            subscribed,
        }
    }

    pub(crate) fn adopt_from(&mut self, source: SubscriptionEvent) {
        self.routing_id = source.routing_id;
        self.topic = source.topic;
        self.subscribed = source.subscribed;
    }
}
