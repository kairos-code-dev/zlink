import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { SupportConversationAllocator } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { SupportConversationAllocator as SupportConversationAllocatorType } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import type { SupportUserActor } from '../Actors/support-user-actor';
import type {
  SetTypingReq,
  SetTypingRes,
  UserIdentity
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('support', PacketNames.setTypingReq)
class SetTypingChannelHandler implements ZLinkRequestHandler<SetTypingReq & UserIdentity, SetTypingRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(SupportConversationAllocator) private readonly conversations: SupportConversationAllocatorType
  ) {}

  async handle(request: SetTypingReq & UserIdentity): Promise<SetTypingRes> {
    const actor = await this.actorManager.getOrCreate(request.actorId, SampleNames.supportActorType) as SupportUserActor;
    return await this.conversations.executeInConversation(request.conversationId, (conversation) => conversation.setTyping(actor, request));
  }
}

export { SetTypingChannelHandler };
