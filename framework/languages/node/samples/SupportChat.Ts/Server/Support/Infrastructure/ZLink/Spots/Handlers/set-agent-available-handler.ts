import { zlinkEntrySpotActorRequestHandler } from '@zlink-systems/nestjs';
import { SupportEntrySpot } from '../support-entry-spot';
import { SupportUserActor } from '../../Actors/support-user-actor';
import { PacketNames } from '../../../../../../Shared/Contracts/messages';
import type {
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import type { SupportEntrySpot as SupportEntrySpotType } from '../support-entry-spot';
import type { SupportUserActor as SupportUserActorType } from '../../Actors/support-user-actor';
import type {
  SetAgentAvailableReq,
  SetAgentAvailableRes
} from '../../../../../../Shared/Contracts/messages';

@zlinkEntrySpotActorRequestHandler({
  actor: () => SupportUserActor,
  entrySpot: () => SupportEntrySpot,
  packetName: PacketNames.setAgentAvailableReq
})
class SetAgentAvailableHandler
  implements ZLinkEntrySpotActorRequestHandler<SupportEntrySpotType, SupportUserActorType, SetAgentAvailableReq, SetAgentAvailableRes> {
  async handle(
    entrySpot: SupportEntrySpotType,
    actor: SupportUserActorType,
    context: ZLinkSpotActorRequestContext,
    request: SetAgentAvailableReq
  ): Promise<SetAgentAvailableRes> {
    void context;
    return entrySpot.setAgentAvailable({
      ...request,
      actorId: actor.actorId,
      displayName: actor.displayName,
      role: actor.role
    });
  }
}

export { SetAgentAvailableHandler };
