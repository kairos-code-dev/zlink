import { Conversation } from '../../../../Domain/SupportChat/conversation';

class ConversationSpot {
  constructor(readonly conversation: Conversation) {}

  join(actorId: string) {
    return this.conversation.join(actorId);
  }
}

export { ConversationSpot };
