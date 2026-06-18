import { SupportUserActor } from './support-user-actor';
import type { SupportUserActor as SupportUserActorType } from './support-user-actor';
import type { ZLinkActorContext, ZLinkActorFactory } from '@zlink-systems/framework';
import type { ActorRefSnapshot } from '../../../../../Shared/Contracts/messages';

class SupportUserActorFactory implements ZLinkActorFactory {
  private readonly actors: Map<string, SupportUserActorType>;

  constructor() {
    this.actors = new Map();
  }

  create(actorId: string, context: ZLinkActorContext): SupportUserActorType {
    const existing = this.actors.get(actorId);
    if (existing !== undefined) {
      Object.defineProperty(existing, 'context', {
        configurable: true,
        enumerable: false,
        value: context
      });
      return existing;
    }
    const actorRef: ActorRefSnapshot = {
      nodeRid: context.spotRid ?? 'supportchat.support.node',
      actorId,
      generation: this.actors.size + 1
    };
    const actor = new SupportUserActor(actorId, actorId, actorRef);
    Object.defineProperty(actor, 'context', {
      configurable: true,
      enumerable: false,
      value: context
    });
    this.actors.set(actorId, actor);
    return actor;
  }

  get(actorId: string): SupportUserActorType {
    const actor = this.actors.get(actorId);
    if (actor === undefined) {
      throw new Error(`Support actor is not available. actor=${actorId}`);
    }
    return actor;
  }
}

export { SupportUserActorFactory };
