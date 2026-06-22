import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { ConversationSpot } from '../../Adapters/ZLink/Spots/conversation-spot';
import { SupportNotificationPublisher } from '../../Adapters/ZLink/Notifications/support-notification-publisher';
import { conversationCreateRequest } from '../../Adapters/ZLink/Spots/conversation-create-request';
import type { ZLinkSpotManager } from '@zlink-systems/framework';
import type { ConversationSpot as ConversationSpotType } from '../../Adapters/ZLink/Spots/conversation-spot';
import type { SupportNotificationPublisher as SupportNotificationPublisherType } from '../../Adapters/ZLink/Notifications/support-notification-publisher';

// SupportConversationAllocator turns a customer identity and subject into a ConversationId
// by assigning a sample-level id, creating that ConversationSpot, and initializing it with
// the create request.
class SupportConversationAllocator {
  private nextConversationId = 1;

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
    const conversationId = `supportchat-conversation-${this.nextConversationId++}`;
    await this.spotManager.getOrCreate(ConversationSpot, conversationId);
    await this.executeInConversation(conversationId, (conversation) => {
      conversation.initializeConversation(
        conversationCreateRequest(customerActorId, customerDisplayName, subject, Date.now())
      );
    });
    return conversationId;
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
