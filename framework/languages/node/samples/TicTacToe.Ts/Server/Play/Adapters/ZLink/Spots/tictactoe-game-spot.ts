import { Message as BindingMessage } from '@zlink-systems/zlink';
import { PlayActorPlaceMarkHandler } from './Handlers/play-actor-place-mark-handler';
import { TicTacToeGameTimerHandler } from './Handlers/tictactoe-game-timer-handler';
import { TicTacToeMatch } from '../../../Domain/TicTacToe/tictactoe-match';
import { GameStatus, PacketNames, gameStateNotify, joinGameRes, placeMarkRes, playerJoinedNotify } from '../../../../../Shared/Contracts/messages';
import type {
  Message,
  ZLinkActor,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext,
  ZLinkTimer
} from '@zlink-systems/framework';
import type {
  JoinGameRes,
  PlaceMarkRes,
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';
import type { TicTacToeMatch as TicTacToeMatchType } from '../../../Domain/TicTacToe/tictactoe-match';

type PlaySpotActor = TicTacToeActor & ZLinkActor;
const GameTickPeriodMs = 1000;

class TicTacToeGameSpot implements ZLinkSpot<PlaySpotActor> {
  readonly context?: ZLinkSpotContext<PlaySpotActor>;
  private roomId: string | null = null;
  private match: TicTacToeMatchType<PlaySpotActor> | null = null;
  private gameTick: ZLinkTimer | null = null;
  private cleanupStarted = false;

  async configure(): Promise<void> {
    this.context?.handlers.actorRequest(PacketNames.placeMarkReq, PlayActorPlaceMarkHandler);
    this.gameTick = await this.context?.addTimer(
      'game-tick',
      GameTickPeriodMs,
      TicTacToeGameTimerHandler
    ) ?? null;
  }

  async onInitialize(): Promise<void> {
    const roomId = this.context?.spotRid;
    if (roomId === undefined) {
      throw new Error('TicTacToeGameSpot requires a Spot context before initialization.');
    }
    this.roomId = roomId;
    this.match = new TicTacToeMatch<PlaySpotActor>(roomId);
  }

  async onClosing(): Promise<void> {
    await this.gameTick?.cancel();
    this.gameTick = null;
  }

  async onActorJoin(actor: PlaySpotActor): Promise<ZLinkSpotActorJoinResponse> {
    try {
      const response = await this.join(actor);
      return { accepted: true, reply: createJsonMessage(response) };
    } catch (error) {
      return {
        accepted: false,
        reply: createJsonMessage({ error: error instanceof Error ? error.message : String(error) })
      };
    }
  }

  async onJoinActor(actor: PlaySpotActor): Promise<void> {
    void actor;
  }

  async onLeaveActor(actor: PlaySpotActor): Promise<void> {
    this.requireMatch().players.delete(actor.actorId);
  }

  async onDisconnectActor(actor: PlaySpotActor): Promise<void> {
    actor.markDisconnected();
  }

  async placeMark(actor: PlaySpotActor, cell: number): Promise<PlaceMarkRes> {
    const match = this.requireMatch();
    const roomId = this.requireRoomId();
    const change = match.placeMark(actor.actorId, cell);
    const state = change.state;
    for (const joined of match.players.values()) {
      if (joined.actorId === actor.actorId) {
        continue;
      }
      await joined.actor.push(PacketNames.gameStateNotify, gameStateNotify(state));
    }
    if (isTerminal(state.status)) {
      setImmediate(() => {
        void this.leaveFinishedActors();
      });
    }
    return placeMarkRes(roomId, actor.actorId, cell, state);
  }

  async tick(): Promise<void> {
    const match = this.requireMatch();
    const change = match.tick();
    if (change.changed) {
      for (const player of match.players.values()) {
        await player.actor.push(PacketNames.gameStateNotify, gameStateNotify(change.state));
      }
    }
    await this.leaveFinishedActors();
  }

  private async join(actor: PlaySpotActor): Promise<JoinGameRes> {
    const match = this.requireMatch();
    const roomId = this.requireRoomId();
    const result = match.joinPlayer(actor);
    actor.roomId = roomId;
    const state = result.state;
    if (result.newlyJoined && match.players.size === 2) {
      for (const player of match.players.values()) {
        if (player.actorId === result.joined.actorId) {
          continue;
        }
        await player.actor.push(
          PacketNames.playerJoinedNotify,
          playerJoinedNotify(roomId, result.joined.actorId, result.joined.mark, state)
        );
        await player.actor.push(PacketNames.gameStateNotify, gameStateNotify(state));
      }
    }
    return joinGameRes(roomId, result.joined.actorId, result.joined.mark, state);
  }

  private async leaveFinishedActors(): Promise<void> {
    const match = this.requireMatch();
    if (this.cleanupStarted || !isTerminal(match.snapshot().status)) {
      return;
    }
    this.cleanupStarted = true;
    for (const player of [...match.players.values()]) {
      player.actor.markForDestroyAfterRoomLeave();
      await this.context?.leaveActor(player.actor);
    }
  }

  private requireRoomId(): string {
    if (this.roomId === null) {
      throw new Error('TicTacToeGameSpot has not been initialized.');
    }
    return this.roomId;
  }

  private requireMatch(): TicTacToeMatchType<PlaySpotActor> {
    if (this.match === null) {
      throw new Error('TicTacToeGameSpot has not been initialized.');
    }
    return this.match;
  }
}

function createJsonMessage(value: unknown): Message {
  return BindingMessage.from(Buffer.from(JSON.stringify(value), 'utf8')) as Message;
}

function isTerminal(status: GameStatus): boolean {
  return status === GameStatus.Won
    || status === GameStatus.Draw
    || status === GameStatus.TurnTimedOut;
}

export { TicTacToeGameSpot };
