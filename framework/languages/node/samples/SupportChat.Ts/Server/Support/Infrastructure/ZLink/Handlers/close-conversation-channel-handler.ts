import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { SupportConversationAllocator } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { SupportConversationAllocator as SupportConversationAllocatorType } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import type {
  CloseConversationReq,
  CloseConversationRes,
  UserIdentity
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('support', PacketNames.closeConversationReq)
class CloseConversationChannelHandler implements ZLinkRequestHandler<CloseConversationReq & UserIdentity, CloseConversationRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(SupportConversationAllocator) private readonly conversations: SupportConversationAllocatorType
  ) {}

  async handle(request: CloseConversationReq & UserIdentity): Promise<CloseConversationRes> {
    await this.actorManager.getOrCreate(request.actorId, SampleNames.supportActorType, request);
    return await this.conversations.executeInConversation(request.conversationId, (conversation) => conversation.close(request, request));
  }
}

export { CloseConversationChannelHandler };
