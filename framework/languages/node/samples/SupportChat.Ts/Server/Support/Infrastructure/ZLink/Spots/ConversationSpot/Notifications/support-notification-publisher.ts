import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_ACTOR_CLIENT } from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../../../Configuration/sample-names';
import { DeliverSupportNotification } from '../../../Actors/support-user-actor';
import { ConversationEventMapper } from './conversation-event-mapper';
import type { ConversationEvent } from '../../../../../Domain/SupportChat/conversation-events';
import type { ActorRef, ZLinkActorClient } from '@zlink-systems/framework';

@Injectable()
class SupportNotificationPublisher {
  private readonly mapper = new ConversationEventMapper();

  constructor(@Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient) {}

  async publish(event: ConversationEvent, recipients: Iterable<ActorRef>): Promise<void> {
    const message = this.mapper.map(event);
    for (const actor of recipients) {
      await this.actors
        .sendToActor(
          SampleNames.conversationSpotMesh,
          actor,
          new DeliverSupportNotification(message, event.state.conversationId)
        )
        .submit();
    }
  }
}

export { SupportNotificationPublisher };
