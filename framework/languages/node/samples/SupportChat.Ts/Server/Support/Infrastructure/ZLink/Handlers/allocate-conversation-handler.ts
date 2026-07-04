import { SupportConversationAllocator } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import type { AllocateConversationReq, AllocateConversationRes } from '../../../../../Shared/Contracts/messages';

class AllocateConversationHandler {
  constructor(private readonly allocator: SupportConversationAllocator) {}

  handle(request: AllocateConversationReq): AllocateConversationRes {
    const conversation = this.allocator.allocate(request.customerActorId, request.subject);
    const state = conversation.snapshot();
    return {
      conversationId: state.conversationId,
      status: state.status
    };
  }
}

export { AllocateConversationHandler };
