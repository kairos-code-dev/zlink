import { Inject } from '@nestjs/common';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { SupportConversationAllocator } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import { ConversationStatuses, PacketNames, allocateConversationRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { SupportConversationAllocator as SupportConversationAllocatorType } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import type {
  AllocateConversationReq,
  AllocateConversationRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('support', PacketNames.allocateConversationReq)
class AllocateConversationHandler implements ZLinkRequestHandler<AllocateConversationReq, AllocateConversationRes> {
  constructor(@Inject(SupportConversationAllocator) private readonly allocator: SupportConversationAllocatorType) {}

  async handle(request: AllocateConversationReq): Promise<AllocateConversationRes> {
    const conversationId = await this.allocator.allocate(
      request.customerActorId,
      request.customerDisplayName,
      request.subject
    );
    return allocateConversationRes(conversationId, ConversationStatuses.WaitingForAgent);
  }
}

export { AllocateConversationHandler };
