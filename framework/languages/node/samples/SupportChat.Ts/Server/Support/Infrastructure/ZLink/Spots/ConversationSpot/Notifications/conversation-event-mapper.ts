import type { ConversationState } from '../../../../../../../Shared/Contracts/messages';

class ConversationEventMapper {
  assigned(conversationId: string, state: ConversationState) {
    return { conversationId, state };
  }
}

export { ConversationEventMapper };
