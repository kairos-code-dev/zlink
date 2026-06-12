const { Inject } = require('@nestjs/common');
const { zlinkRequestHandler } = require('../../../../../../../../packages/nestjs/dist');
const { PlayerActorFactory } = require('../Actors/player-actor-factory');
const { BingoEntrySpot } = require('../Spots/bingo-entry-spot');
const { MatchBingoActorHandler } = require('../Spots/Handlers/match-bingo-actor-handler');
const { PacketNames } = require('../../../../../Shared/Contracts/messages');
import type {
  ZLinkRequestHandler,
  ZLinkSpotActorRequestContext
} from '../../../../../../../packages/framework/dist';
import type { PlayerActorFactory as PlayerActorFactoryType } from '../Actors/player-actor-factory';
import type { BingoEntrySpot as BingoEntrySpotType } from '../Spots/bingo-entry-spot';
import type { MatchBingoActorHandler as MatchBingoActorHandlerType } from '../Spots/Handlers/match-bingo-actor-handler';
import type {
  MatchBingoReq,
  MatchBingoRes,
  PlayerIdentity
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.matchBingoReq)
class MatchBingoChannelHandler implements ZLinkRequestHandler<MatchBingoReq & PlayerIdentity, MatchBingoRes> {
  constructor(
    @Inject(PlayerActorFactory) private readonly actorFactory: PlayerActorFactoryType,
    @Inject(BingoEntrySpot) private readonly entrySpot: BingoEntrySpotType,
    @Inject(MatchBingoActorHandler) private readonly matchBingoActor: MatchBingoActorHandlerType
  ) {}

  async handle(request: MatchBingoReq & PlayerIdentity): Promise<MatchBingoRes> {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    return await this.matchBingoActor.handle(this.entrySpot, actor, createActorRequestContext(PacketNames.matchBingoReq), request);
  }
}

function createActorRequestContext(packetName: string): ZLinkSpotActorRequestContext {
  return {
    packetName,
    metadata: {},
    reply: {
      metadata() {
        return this;
      },
      compress() {
        return this;
      }
    }
  };
}

export { MatchBingoChannelHandler };
