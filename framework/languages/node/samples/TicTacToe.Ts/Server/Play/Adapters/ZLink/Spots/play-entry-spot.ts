const { Inject } = require('@nestjs/common');
const { ModuleRef } = require('@nestjs/core');
const { PlayActorJoinGameHandler } = require('./Handlers/play-actor-join-game-handler');
const { TicTacToeGameCreator } = require('../../../Application/GameCreation/tictactoe-game-creator');
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
import type { TicTacToeGameCreator as TicTacToeGameCreatorType, TicTacToeRoom } from '../../../Application/GameCreation/tictactoe-game-creator';

class PlayEntrySpot implements ZLinkEntrySpot {
  readonly context!: ZLinkEntrySpotContext;
  private joinHandler: InstanceType<typeof PlayActorJoinGameHandler> | null;

  constructor(
    private readonly moduleRef: NestModuleRef,
    private readonly games: TicTacToeGameCreatorType
  ) {
    this.moduleRef = moduleRef;
    this.games = games;
    this.joinHandler = null;
    this.games.setFinishedRoomCleaner((room) => this.cleanupFinishedRoom(room));
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

  async onPostActorJoined(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    if (isDestroyMarked(actor)) {
      await this.context.destroyActor(actor, signal);
    }
  }

  async cleanupFinishedRoom(room: TicTacToeRoom): Promise<void> {
    for (const player of room.match.players.values()) {
      player.actor.markForDestroyAfterRoomLeave();
      room.match.players.delete(player.actorId);
      await this.onPostActorJoined(toZLinkActor(player.actor));
    }
  }
}

function toZLinkActor(actor: TicTacToeActor): ZLinkActor {
  const candidate = actor as Partial<ZLinkActor>;
  if (candidate.context === undefined) {
    throw new Error(`Actor '${actor.actorId}' is not attached to a ZLink actor context.`);
  }
  return candidate as ZLinkActor;
}

function toTicTacToeActor(actor: ZLinkActor): TicTacToeActor {
  const candidate = actor as ZLinkActor & Partial<TicTacToeActor>;
  if (typeof candidate.markDisconnected !== 'function') {
    throw new Error(`Actor '${actor.actorId}' is not a tic-tac-toe player actor.`);
  }
  return candidate as TicTacToeActor;
}

function isDestroyMarked(actor: ZLinkActor): boolean {
  return (actor as Partial<TicTacToeActor>).destroyAfterEntrySpotJoin === true;
}

Inject(ModuleRef)(PlayEntrySpot, undefined, 0);
Inject(TicTacToeGameCreator)(PlayEntrySpot, undefined, 1);

export { PlayEntrySpot };
