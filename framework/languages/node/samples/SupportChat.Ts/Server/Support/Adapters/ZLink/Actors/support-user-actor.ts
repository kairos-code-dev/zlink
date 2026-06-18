import type { ActorRefSnapshot } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActor, ZLinkActorContext } from '@zlink-systems/framework';

class SupportUserActor implements ZLinkActor {
  readonly context!: ZLinkActorContext;

  constructor(
    readonly actorId: string,
    public displayName: string,
    readonly actorRef: ActorRefSnapshot,
    public role = '',
    public conversationId = '',
    public disconnected = false
  ) {
    this.actorId = actorId;
    this.displayName = displayName;
    this.actorRef = actorRef;
  }

  setIdentity(displayName: string, role: string): void {
    this.displayName = displayName;
    this.role = role;
  }

  joinConversation(conversationId: string): void {
    this.conversationId = conversationId;
  }

  markDisconnected(): void {
    this.disconnected = true;
  }
}

export { SupportUserActor };
