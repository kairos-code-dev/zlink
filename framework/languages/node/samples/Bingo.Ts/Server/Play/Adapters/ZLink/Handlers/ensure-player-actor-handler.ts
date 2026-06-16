import { Inject } from '@nestjs/common';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PlayerActorFactory } from '../Actors/player-actor-factory';
import { PacketNames, ensurePlayerActorRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
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
