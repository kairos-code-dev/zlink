import { Inject } from '@nestjs/common';
import { ConversationEventMapper } from './conversation-event-mapper';
import { SupportNotificationDeliveryLog } from '../../../notification-delivery-log';
import { SampleNames } from '../../../../Configuration/sample-names';
import type { ConversationEventMapper as ConversationEventMapperType } from './conversation-event-mapper';
import type { SupportNotificationDeliveryLog as SupportNotificationDeliveryLogType } from '../../../notification-delivery-log';
import type { ConversationEvent } from '../../../Domain/SupportChat/conversation-models';
import type { ConversationState, ParticipantJoinedNotify } from '../../../../../Shared/Contracts/messages';

// SupportNotificationPublisher is the outbound NotificationPort implementation. It maps each
// domain ConversationEvent to bound-session push frames and appends them to the delivery log,
// which the Session server pumps to each bound stream session.
class SupportNotificationPublisher {
  constructor(
    private readonly mapper: ConversationEventMapperType,
    private readonly deliveryLog: SupportNotificationDeliveryLogType
  ) {}

  async publish(events: ConversationEvent[], participantActorIds: string[]): Promise<void> {
    for (const conversationEvent of events) {
      for (const mapped of this.mapper.map(conversationEvent, participantActorIds)) {
        this.deliveryLog.append(mapped.actorId, mapped.packetName, mapped.payload);
      }
    }
  }

  async publishJoinedAgentToCustomer(customerActorId: string, state: ConversationState): Promise<void> {
    if (state.agentActorId === null) {
      return;
    }
    const payload: ParticipantJoinedNotify = {
      conversationId: state.conversationId,
      actorId: state.agentActorId,
      role: 'Agent',
      state
    };
    this.deliveryLog.append(customerActorId, SampleNames.participantJoinedPacket, payload);
  }
}

Inject(ConversationEventMapper)(SupportNotificationPublisher, undefined, 0);
Inject(SupportNotificationDeliveryLog)(SupportNotificationPublisher, undefined, 1);

export { SupportNotificationPublisher };
