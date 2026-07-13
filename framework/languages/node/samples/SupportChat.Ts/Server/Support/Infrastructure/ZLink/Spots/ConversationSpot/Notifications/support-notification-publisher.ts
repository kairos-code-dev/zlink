import { ConversationEventMapper } from './conversation-event-mapper';
import type { ConversationEvent } from '../../../../../Domain/SupportChat/conversation-events';
import type { SupportUserActor } from '../../../Actors/support-user-actor';

class SupportNotificationPublisher {
  constructor(private readonly mapper = new ConversationEventMapper()) {}

  publish(event: ConversationEvent, recipients: Iterable<SupportUserActor>): void {
    const message = this.mapper.map(event);
    for (const actor of recipients) {
      actor.push(message, event.state.conversationId);
    }
  }
}

export { SupportNotificationPublisher };
