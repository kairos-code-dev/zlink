const { Inject } = require('@nestjs/common');
const { PlayerActorFactory } = require('../Actors/player-actor-factory');
const { ensurePlayerActorRes } = require('../../../../../Shared/Contracts/messages');
import type {
  EnsurePlayerActorReq,
  EnsurePlayerActorRes
} from '../../../../../Shared/Contracts/messages';

class EnsurePlayerActorHandler {
  [key: string]: any;
  constructor(actorFactory: any) {
    this.actorFactory = actorFactory;
  }
  async handle(request: EnsurePlayerActorReq): Promise<EnsurePlayerActorRes> {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    return ensurePlayerActorRes(actor);
  }
}

Inject(PlayerActorFactory)(EnsurePlayerActorHandler, undefined, 0);

export { EnsurePlayerActorHandler };
