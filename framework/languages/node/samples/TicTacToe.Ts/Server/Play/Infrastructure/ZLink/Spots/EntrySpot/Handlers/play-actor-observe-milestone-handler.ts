import { Injectable } from '@nestjs/common';
import { ZLinkSpotActorRequest } from '@zlink-systems/framework';
import { observeMilestoneRes, PacketNames } from '../../../../../../../Shared/Contracts/messages';
import type {
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import type {
  ObserveMilestoneReq,
  ObserveMilestoneRes,
} from '../../../../../../../Shared/Contracts/messages';
import type { PlayActor } from '../../../Actors/play-actor';
import { MilestoneObserverRegistry } from '../play-entry-spot';

@Injectable()
class PlayActorObserveMilestoneHandler {
  constructor(private readonly observers: MilestoneObserverRegistry) {}

  @ZLinkSpotActorRequest(PacketNames.observeMilestoneReq)
  async handle(
    actor: PlayActor,
    context: ZLinkSpotActorRequestContext,
    _request: ObserveMilestoneReq
  ): Promise<ObserveMilestoneRes> {
    void context;
    this.observers.subscribe(actor.actorId);
    return observeMilestoneRes(true);
  }
}

export { PlayActorObserveMilestoneHandler };
