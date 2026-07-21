import { Injectable } from '@nestjs/common';
import { AgentAvailabilityDirectory } from '../../../../Application/ConversationAssignment/agent-availability-directory';
import { SupportActorDirectory } from '../../Actors/support-actor-directory';
import { SupportUserActor } from '../../Actors/support-user-actor';
import type { EnsureSupportUserActorReq } from '../../../../../../Shared/Contracts/messages';
import type {
  ZLinkActorJoinRequest,
  ZLinkActorMembership,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse
} from '@zlink-systems/framework';

@Injectable()
class SupportEntrySpot implements ZLinkEntrySpot<SupportUserActor> {
  readonly context!: ZLinkEntrySpotContext<SupportUserActor, SupportEntrySpot>;

  constructor(
    private readonly availability: AgentAvailabilityDirectory,
    private readonly directory: SupportActorDirectory
  ) {}

  async onActorJoin(
    _actor: ZLinkActorJoinRequest,
    _request: ZLinkMessage
  ): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }

  async onCreateActor(actor: ZLinkActorMembership, request: ZLinkMessage): Promise<void> {
    const value = request.decode<EnsureSupportUserActorReq>(Object as never);
    this.directory.bind(actor, {
      displayName: value.displayName,
      role: value.role,
      participantId: value.participantId
    });
  }

  async onJoinedActor(_actor: ZLinkActorMembership): Promise<void> {}
  async onLeaveActor(_actor: ZLinkActorMembership): Promise<void> {}

  async onDisconnectActor(actor: ZLinkActorMembership): Promise<void> {
    const identity = this.directory.get(actor.actor.actorId);
    if (identity?.role === 'Agent' && identity.actorId === identity.participantId) {
      this.availability.setAvailable(identity.actorId, false);
    }
  }
}

export { SupportEntrySpot };
