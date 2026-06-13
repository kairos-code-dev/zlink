const { Message: BindingMessage } = require('@zlink-systems/zlink');
const {
  PacketNames,
  gameStateNotify,
  joinGameRes,
  placeMarkRes,
  playerJoinedNotify
} = require('../../../../../Shared/Contracts/messages');
import type {
  Message,
  ZLinkActor,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext
} from '../../../../../../../packages/framework/dist';
import type {
  JoinGameRes,
  PlaceMarkRes,
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';
import type { TicTacToeRoom } from '../../../Application/GameCreation/tictactoe-game-creator';

type PlaySpotActor = TicTacToeActor & ZLinkActor;

class TicTacToeGameSpot implements ZLinkSpot<PlaySpotActor> {
  readonly context?: ZLinkSpotContext;
  private room: TicTacToeRoom | null;
  private cleanupStarted = false;

  constructor(context?: ZLinkSpotContext) {
    this.context = context;
    this.room = null;
  }

  configureRoom(room: TicTacToeRoom): void {
    this.room = room;
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
    const room = this.requireRoom();
    room.match.players.delete(actor.actorId);
  }

  async onDisconnectActor(actor: PlaySpotActor): Promise<void> {
    actor.markDisconnected();
  }

  async placeMark(actor: PlaySpotActor, cell: number): Promise<PlaceMarkRes> {
    const room = this.requireRoom();
    const change = room.match.placeMark(actor.actorId, cell);
    const state = change.state;
    for (const joined of room.match.players.values()) {
      if (joined.actorId === actor.actorId) {
        continue;
      }
      await joined.actor.push(PacketNames.gameStateNotify, gameStateNotify(state));
    }
    if (state.status === 'Won' || state.status === 'Draw' || state.status === 'TurnTimedOut') {
      setImmediate(() => {
        void this.leaveFinishedActors();
      });
    }
    return placeMarkRes(room.roomId, actor.actorId, cell, state);
  }

  private async join(actor: PlaySpotActor): Promise<JoinGameRes> {
    const room = this.requireRoom();
    const result = room.match.joinPlayer(actor);
    actor.roomId = room.roomId;
    const state = result.state;
    if (result.newlyJoined && room.match.players.size === 2) {
      for (const player of room.match.players.values()) {
        if (player.actorId === result.joined.actorId) {
          continue;
        }
        await player.actor.push(
          PacketNames.playerJoinedNotify,
          playerJoinedNotify(room.roomId, result.joined.actorId, result.joined.mark, state)
        );
        await player.actor.push(PacketNames.gameStateNotify, gameStateNotify(state));
      }
    }
    return joinGameRes(room.roomId, result.joined.actorId, result.joined.mark, state);
  }

  private async leaveFinishedActors(): Promise<void> {
    const room = this.requireRoom();
    if (this.cleanupStarted || !isTerminal(room.match.snapshot().status)) {
      return;
    }
    this.cleanupStarted = true;
    for (const player of [...room.match.players.values()]) {
      player.actor.markForDestroyAfterRoomLeave();
      await this.context?.leaveActor(player.actor as PlaySpotActor);
    }
  }

  private requireRoom(): TicTacToeRoom {
    if (this.room === null) {
      throw new Error(`TicTacToe room '${this.context?.spotRid ?? 'unknown'}' has not been configured.`);
    }
    return this.room;
  }
}

function createJsonMessage(value: unknown): Message {
  return BindingMessage.from(Buffer.from(JSON.stringify(value), 'utf8')) as Message;
}

function isTerminal(status: string): boolean {
  return status === 'Won' || status === 'Draw' || status === 'TurnTimedOut';
}

export { TicTacToeGameSpot };
