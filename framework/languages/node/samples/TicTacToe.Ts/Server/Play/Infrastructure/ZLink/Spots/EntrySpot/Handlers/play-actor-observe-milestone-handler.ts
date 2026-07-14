import { Injectable } from '@nestjs/common';
import { ZLinkSpotActorRequest } from '@zlink-systems/framework';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import type {
  ZLinkActor,
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import type {
  ObserveMilestoneReq,
  ObserveMilestoneRes,
  TicTacToeActor
} from '../../../../../../../Shared/Contracts/messages';
import type { PlayEntrySpot } from '../play-entry-spot';

type PlayObserveActor = TicTacToeActor & ZLinkActor;

@Injectable()
class PlayActorObserveMilestoneHandler
  implements ZLinkEntrySpotActorRequestHandler<
    PlayEntrySpot,
    PlayObserveActor,
    ObserveMilestoneReq,
    ObserveMilestoneRes
  > {
  @ZLinkSpotActorRequest(PacketNames.observeMilestoneReq)
  async handle(
    entrySpot: PlayEntrySpot,
    actor: PlayObserveActor,
    context: ZLinkSpotActorRequestContext
  ): Promise<ObserveMilestoneRes> {
    void context;
    return await entrySpot.observeMilestone(actor);
  }
}

export { PlayActorObserveMilestoneHandler };
