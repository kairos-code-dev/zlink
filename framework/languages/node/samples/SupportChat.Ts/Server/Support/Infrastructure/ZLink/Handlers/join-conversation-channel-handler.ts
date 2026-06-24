import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { SupportConversationAllocator } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames, joinConversationRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { SupportConversationAllocator as SupportConversationAllocatorType } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import type {
  JoinConversationReq,
  JoinConversationRes,
  UserIdentity
} from '../../../../../Shared/Contracts/messages';

// Reconnect path: the same actor re-confirms current conversation state with JoinConversationReq.
// The conversation membership and Subject are preserved because the actor and Spot are unchanged.
@zlinkRequestHandler('support', PacketNames.joinConversationReq)
class JoinConversationChannelHandler implements ZLinkRequestHandler<JoinConversationReq & UserIdentity, JoinConversationRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(SupportConversationAllocator) private readonly conversations: SupportConversationAllocatorType
  ) {}

  async handle(request: JoinConversationReq & UserIdentity): Promise<JoinConversationRes> {
    await this.actorManager.getOrCreate(request.actorId, SampleNames.supportActorType, request);
    const state = await this.conversations.executeInConversation(request.conversationId, (conversation) => conversation.snapshot());
    return joinConversationRes(state);
  }
}

export { JoinConversationChannelHandler };
