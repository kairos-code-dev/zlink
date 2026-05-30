use crate::routing_id::RoutingId;

/// A subscriber's subscribe or unsubscribe as observed by an XPUB socket.
pub struct SubscriptionEvent {
    /// The subscriber's routing id, when known.
    pub routing_id: Option<RoutingId>,
    /// The topic that was subscribed or unsubscribed.
    pub topic: smol_str::SmolStr,
    /// `true` for a subscribe, `false` for an unsubscribe.
    pub subscribed: bool,
}

impl SubscriptionEvent {
    /// Creates an empty reusable event; reuse it across
    /// `receive_subscription_event` calls.
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
