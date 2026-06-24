import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { SupportEntrySpot } from '../Spots/support-entry-spot';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { SupportEntrySpot as SupportEntrySpotType } from '../Spots/support-entry-spot';
import type {
  SetAgentAvailableReq,
  SetAgentAvailableRes,
  UserIdentity
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('support', PacketNames.setAgentAvailableReq)
class SetAgentAvailableChannelHandler implements ZLinkRequestHandler<SetAgentAvailableReq & UserIdentity, SetAgentAvailableRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(SupportEntrySpot) private readonly entrySpot: SupportEntrySpotType
  ) {}

  async handle(request: SetAgentAvailableReq & UserIdentity): Promise<SetAgentAvailableRes> {
    await this.actorManager.getOrCreate(request.actorId, SampleNames.supportActorType, request);
    return this.entrySpot.setAgentAvailable(request);
  }
}

export { SetAgentAvailableChannelHandler };
