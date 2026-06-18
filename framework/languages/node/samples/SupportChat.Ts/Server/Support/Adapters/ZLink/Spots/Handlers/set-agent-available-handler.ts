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

// Only agent actors may register availability. A customer actor that sends this request
// receives an error response (the entry spot rejects the wrong role).
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
    try {
      return entrySpot.setAgentAvailable(actor, request);
    } catch (error) {
      return { isAvailable: false, error: error instanceof Error ? error.message : String(error) };
    }
  }
}

export { SetAgentAvailableHandler };
