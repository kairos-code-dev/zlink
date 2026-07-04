import { allocateConversation } from '../../../../Shared/Contracts/messages';

class ConversationStarter {
  start(customerActorId: string, displayName: string, subject: string) {
    return allocateConversation(customerActorId, displayName, subject);
  }
}

export { ConversationStarter };
