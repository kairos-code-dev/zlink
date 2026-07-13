import { Injectable } from '@nestjs/common';
import { AgentAvailabilityDirectory } from '../../../../Application/ConversationAssignment/agent-availability-directory';
import type { EnsureSupportUserActorReq } from '../../../../../../Shared/Contracts/messages';
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse
} from '@zlink-systems/framework';
import type { SupportUserActor } from '../../Actors/support-user-actor';

@Injectable()
class SupportEntrySpot implements ZLinkEntrySpot<SupportUserActor> {
  readonly context!: ZLinkEntrySpotContext<SupportUserActor, SupportEntrySpot>;

  constructor(private readonly availability: AgentAvailabilityDirectory) {}

  async onActorJoin(_actorId: string, _request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }

  async onCreateActor(actor: SupportUserActor, request: ZLinkMessage): Promise<void> {
    const value = request.decode<EnsureSupportUserActorReq>(Object as never);
    actor.initializeIdentity(value.displayName, value.role, value.participantId);
  }

  async onJoinedActor(_actor: SupportUserActor): Promise<void> {}
  async onLeaveActor(_actor: SupportUserActor): Promise<void> {}

  async onDisconnectActor(actor: SupportUserActor): Promise<void> {
    if (actor.role === 'Agent' && actor.actorId === actor.participantId) {
      this.availability.setAvailable(actor.actorId, false);
    }
  }
}

export { SupportEntrySpot };
