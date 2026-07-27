import { Inject, Injectable } from '@nestjs/common';
import {
  ZLINK_SPOT_MANAGER,
  ZLINK_SPOT_OUTBOUND
} from '@zlink-systems/nestjs';
import { ZLinkSpotActorRequest } from '@zlink-systems/framework';
import { SampleNames } from '../../../../../../Configuration/sample-settings';
import type {
  ZLinkSpotActorRequestContext,
  ZLinkSpotManager,
  ZLinkSpotOutbound
} from '@zlink-systems/framework';
import type { PlayActor } from '../../../Actors/play-actor';
import type { PlaceMarkReq, PlaceMarkRes } from '../../../../../../../Shared/Contracts/messages';
import { PlaceMarkAtGameSpotReq } from './tictactoe-game-operation-handlers';

@Injectable()
class PlayActorPlaceMarkHandler {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spotHandles: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly spotOutbound: ZLinkSpotOutbound
  ) {}

  @ZLinkSpotActorRequest('PlaceMarkReq')
  async handle(
    actor: PlayActor,
    _context: ZLinkSpotActorRequestContext,
    request: PlaceMarkReq
  ): Promise<PlaceMarkRes> {
    const spotRid = actor.context.spotRid;
    if (spotRid === undefined) {
      throw new Error(`Actor '${actor.actorId}' is not joined to a game.`);
    }
    const spot = await this.spotHandles.find(spotRid);
    if (spot === undefined) {
      throw new Error(`Game spot '${String(spotRid)}' could not be resolved.`);
    }
    return this.spotOutbound
      .requestToSpot(spot, new PlaceMarkAtGameSpotReq(actor.actorId, request.cell))
      .yield<PlaceMarkRes>();
  }
}

export { PlayActorPlaceMarkHandler };
