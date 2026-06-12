const { Inject } = require('@nestjs/common');
const { ModuleRef } = require('@nestjs/core');
const { PlayActorJoinGameHandler } = require('./Handlers/play-actor-join-game-handler');
import type { ModuleRef as NestModuleRef } from '@nestjs/core';
import type {
  ZLinkActor,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext
} from '../../../../../../../packages/framework/dist';
import type {
  JoinGameRes,
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';

class PlayEntrySpot implements ZLinkEntrySpot {
  readonly context!: ZLinkEntrySpotContext;
  private joinHandler: InstanceType<typeof PlayActorJoinGameHandler> | null;

  constructor(private readonly moduleRef: NestModuleRef) {
    this.moduleRef = moduleRef;
    this.joinHandler = null;
  }

  async join(actor: TicTacToeActor, roomId: string): Promise<JoinGameRes> {
    this.joinHandler ??= await this.moduleRef.create(PlayActorJoinGameHandler);
    return await this.joinHandler.handle({ actor, roomId });
  }

  async onActorDisconnected(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    void signal;
    const player = toTicTacToeActor(actor);
    player.markDisconnected();
  }
}

function toTicTacToeActor(actor: ZLinkActor): TicTacToeActor {
  const candidate = actor as ZLinkActor & Partial<TicTacToeActor>;
  if (typeof candidate.markDisconnected !== 'function') {
    throw new Error(`Actor '${actor.actorId}' is not a tic-tac-toe player actor.`);
  }
  return candidate as TicTacToeActor;
}

Inject(ModuleRef)(PlayEntrySpot, undefined, 0);

export { PlayEntrySpot };
