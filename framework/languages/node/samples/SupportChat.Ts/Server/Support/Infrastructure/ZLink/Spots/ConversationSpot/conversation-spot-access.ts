import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { SupportNotificationPublisher } from './Notifications/support-notification-publisher';
import { conversationCreateRequest } from './conversation-create-request';
import { ConversationSpot } from './conversation-spot';
import type { ZLinkSpotManager } from '@zlink-systems/framework';
import type {
  ConversationExecutor,
  ConversationStartRequest,
  ConversationStarter
} from '../../../../Application/ConversationAssignment/support-conversation-allocator';

class ZLinkConversationSpotAccess implements ConversationStarter, ConversationExecutor {
  constructor(
    private readonly notifications: SupportNotificationPublisher,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spotManager: ZLinkSpotManager
  ) {
    ConversationSpot.useNotifications(notifications);
  }

  async start(request: ConversationStartRequest): Promise<void> {
    await this.spotManager.getOrCreate(ConversationSpot, request.conversationId);
    await this.execute(request.conversationId, (conversation) => {
      conversation.initializeConversation(
        conversationCreateRequest(
          request.customerActorId,
          request.customerDisplayName,
          request.subject,
          request.createdAtUnixMs
        )
      );
    });
  }

  async execute<TResult>(
    conversationId: string,
    operation: (conversation: ConversationSpot) => TResult | Promise<TResult>
  ): Promise<TResult> {
    return await this.spotManager.executeOnSpot<ConversationSpot, TResult>(ConversationSpot, conversationId, operation);
  }
}

Inject(SupportNotificationPublisher)(ZLinkConversationSpotAccess, undefined, 0);

export { ZLinkConversationSpotAccess };
