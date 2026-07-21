import type { ActorRef, ZLinkActorMembership } from '@zlink-systems/framework';
import type { SupportRole } from '../../../../../Shared/Contracts/messages';

interface SupportActorIdentity {
  readonly actor: ActorRef;
  readonly actorId: string;
  readonly displayName: string;
  readonly role: SupportRole;
  readonly participantId: string;
}

class SupportActorDirectory {
  private readonly actors = new Map<string, SupportActorIdentity>();

  bind(actor: ZLinkActorMembership, identity: Omit<SupportActorIdentity, 'actor' | 'actorId'>): void {
    this.bindActor(actor.actor, identity);
  }

  bindActor(actor: ActorRef, identity: Omit<SupportActorIdentity, 'actor' | 'actorId'>): void {
    this.actors.set(actor.actorId, {
      actor,
      actorId: actor.actorId,
      ...identity
    });
  }

  get(actorId: string): SupportActorIdentity | undefined {
    return this.actors.get(actorId);
  }

  remove(actorId: string): void {
    this.actors.delete(actorId);
  }
}

export { SupportActorDirectory };
export type { SupportActorIdentity };
