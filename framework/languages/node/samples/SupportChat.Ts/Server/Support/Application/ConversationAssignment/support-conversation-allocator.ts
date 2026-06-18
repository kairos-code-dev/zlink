import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { ZLinkSpotCreateState } from '@zlink-systems/framework';
import { ConversationSpot } from '../../Adapters/ZLink/Spots/conversation-spot';
import { SupportNotificationPublisher } from '../../Adapters/ZLink/Notifications/support-notification-publisher';
import { conversationCreateRequest } from '../../Adapters/ZLink/Spots/conversation-create-request';
import type { ZLinkSpotManager } from '@zlink-systems/framework';
import type { ConversationSpot as ConversationSpotType } from '../../Adapters/ZLink/Spots/conversation-spot';
import type { SupportNotificationPublisher as SupportNotificationPublisherType } from '../../Adapters/ZLink/Notifications/support-notification-publisher';

// SupportConversationAllocator turns a customer identity and subject into a ConversationId
// by creating a ConversationSpot and initializing it with the create request. The Spot
// routing id is derived by the framework Spot manager; the ConversationId is that routing id.
class SupportConversationAllocator {
  constructor(
    private readonly notifications: SupportNotificationPublisherType,
    private readonly spotManager: ZLinkSpotManager
  ) {
    ConversationSpot.useNotifications(notifications);
  }

  async allocate(
    customerActorId: string,
    customerDisplayName: string,
    subject: string
  ): Promise<string> {
    const created = await this.spotManager.create(ConversationSpot);
    if (created.state !== ZLinkSpotCreateState.Created) {
      throw new Error('Support conversation creation was rejected.');
    }
    await this.executeInConversation(created.spotRid, (conversation) => {
      conversation.initializeConversation(
        conversationCreateRequest(customerActorId, customerDisplayName, subject, Date.now())
      );
    });
    return created.spotRid;
  }

  async executeInConversation<TResult>(
    conversationId: string,
    operation: (conversation: ConversationSpotType) => TResult | Promise<TResult>
  ): Promise<TResult> {
    return await this.spotManager.executeOnSpot<ConversationSpotType, TResult>(ConversationSpot, conversationId, operation);
  }
}

Inject(SupportNotificationPublisher)(SupportConversationAllocator, undefined, 0);
Inject(ZLINK_SPOT_MANAGER)(SupportConversationAllocator, undefined, 1);

export { SupportConversationAllocator };
