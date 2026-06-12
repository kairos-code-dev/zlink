const { Inject } = require('@nestjs/common');
const { PlayerActorFactory } = require('../Actors/player-actor-factory');
const { ensurePlayerActorRes } = require('../../../../../Shared/Contracts/messages');
import type { ZLinkRequestHandler } from '../../../../../../../packages/framework/dist';
import type { PlayerActorFactory as PlayerActorFactoryType } from '../Actors/player-actor-factory';
import type {
  EnsurePlayerActorReq,
  EnsurePlayerActorRes
} from '../../../../../Shared/Contracts/messages';

class EnsurePlayerActorHandler implements ZLinkRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes> {
  constructor(private readonly actorFactory: PlayerActorFactoryType) {}
  async handle(request: EnsurePlayerActorReq): Promise<EnsurePlayerActorRes> {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    return ensurePlayerActorRes(actor);
  }
}

Inject(PlayerActorFactory)(EnsurePlayerActorHandler, undefined, 0);

export { EnsurePlayerActorHandler };
