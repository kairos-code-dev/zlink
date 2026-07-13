import type { SupportRole } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActor, ZLinkActorContext } from '@zlink-systems/framework';

class SupportUserActor implements ZLinkActor {
  displayName = '';
  role: SupportRole = 'Customer';
  participantId: string;

  constructor(readonly actorId: string, readonly context: ZLinkActorContext) {
    this.participantId = actorId;
  }

  initializeIdentity(displayName: string, role: SupportRole, participantId: string): void {
    this.displayName = displayName;
    this.role = role;
    this.participantId = participantId;
  }

  push(message: unknown, conversationId: string): void {
    try {
      this.context.boundSession
        .send(message)
        .metadata('conversation-id', conversationId)
        .submit();
    } catch {
      // Conversation state remains in the Spot and is returned after reconnect.
    }
  }
}

export { SupportUserActor };
