const { Inject } = require('@nestjs/common');
const { zlinkRequestHandler } = require('../../../../../../../../packages/nestjs/dist');
const { PlayerActorFactory } = require('../Actors/player-actor-factory');
const { PacketNames, ensurePlayerActorRes } = require('../../../../../Shared/Contracts/messages');
import type { ZLinkRequestHandler } from '../../../../../../../packages/framework/dist';
import type { PlayerActorFactory as PlayerActorFactoryType } from '../Actors/player-actor-factory';
import type {
  EnsurePlayerActorReq,
  EnsurePlayerActorRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.ensurePlayerActorReq)
class EnsurePlayerActorHandler implements ZLinkRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes> {
  constructor(@Inject(PlayerActorFactory) private readonly actorFactory: PlayerActorFactoryType) {}

  async handle(request: EnsurePlayerActorReq): Promise<EnsurePlayerActorRes> {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    return ensurePlayerActorRes(actor);
  }
}

export { EnsurePlayerActorHandler };
