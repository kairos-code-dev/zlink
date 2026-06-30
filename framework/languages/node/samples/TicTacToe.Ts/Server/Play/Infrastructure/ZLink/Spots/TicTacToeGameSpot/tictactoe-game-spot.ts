import { PlayActorLeaveGameHandler } from './Handlers/play-actor-leave-game-handler';
import { PlayActorPlaceMarkHandler } from './Handlers/play-actor-place-mark-handler';
import { TicTacToeGameTimerHandler } from './Handlers/tictactoe-game-timer-handler';
import { TicTacToeMatch } from '../../../../Domain/TicTacToe/tictactoe-match';
import {
  GameStatus,
  PacketNames,
  gameStateNotify,
  joinGameRes,
  placeMarkRes,
  playerJoinedNotify,
  playerWinMilestoneEvent
} from '../../../../../../Shared/Contracts/messages';
import { SampleDefaults, SampleNames } from '../../../../../Configuration/sample-settings';
import type {
  ZLinkActor,
  ZLinkMessage,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext,
  ZLinkTimer
} from '@zlink-systems/framework';
import type {
  JoinGameRes,
  GameState,
  PlaceMarkRes,
  TicTacToeGameJoinReq,
  TicTacToeActor
} from '../../../../../../Shared/Contracts/messages';
import type { TicTacToeMatch as TicTacToeMatchType } from '../../../../Domain/TicTacToe/tictactoe-match';

type PlaySpotActor = TicTacToeActor & ZLinkActor;
const GameTickPeriodMs = 1000;
const InitialRoomId = 'tictactoe-room';

class TicTacToeGameSpot implements ZLinkSpot<PlaySpotActor> {
  readonly context!: ZLinkSpotContext<PlaySpotActor>;
  private roomId = InitialRoomId;
  private match: TicTacToeMatchType<PlaySpotActor> = new TicTacToeMatch<PlaySpotActor>(InitialRoomId);
  private gameTick?: ZLinkTimer;

  async configure(): Promise<void> {
    this.context.handlers.actorRequest(PacketNames.placeMarkReq, PlayActorPlaceMarkHandler);
    this.context.handlers.actorSend(PacketNames.leaveGameMsg, PlayActorLeaveGameHandler);
    this.gameTick = await this.context.addTimer(
      'game-tick',
      GameTickPeriodMs,
      TicTacToeGameTimerHandler
    );
  }

  async onInitialize(): Promise<void> {
    this.roomId = this.context.spotRid;
    this.match = new TicTacToeMatch<PlaySpotActor>(this.roomId);
  }

  async onClosing(): Promise<void> {
    await this.gameTick?.cancel();
    this.gameTick = undefined;
  }

  async onActorJoin(actor: PlaySpotActor, requestMessage: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    try {
      console.log(`game spot: onActorJoin received. actor=${actor.actorId} roomId=${this.roomId}`);
      const request = requestMessage.decode<TicTacToeGameJoinReq>();
      const response = await this.join(actor, request);
      console.log(`game spot: onActorJoin completed. actor=${actor.actorId} roomId=${this.roomId}`);
      return { accepted: true, reply: response };
    } catch (error) {
      return {
        accepted: false,
        reply: { error: error instanceof Error ? error.message : String(error) }
      };
    }
  }

  async onJoinedActor(actor: PlaySpotActor): Promise<void> {
    console.log(`game spot: actor joined. actor=${actor.actorId} roomId=${this.roomId}`);
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
    const before = match.snapshot();
    const change = match.placeMark(actor.actorId, cell);
    const state = change.state;
    for (const joined of match.players.values()) {
      if (joined.actorId === actor.actorId) {
        continue;
      }
      await joined.actor.push(PacketNames.gameStateNotify, gameStateNotify(state));
    }
    await this.publishWinMilestone(actor, before, state);
    return placeMarkRes(state);
  }

  async tick(): Promise<void> {
    const match = this.requireMatch();
    const change = match.tick();
    if (change.changed) {
      for (const player of match.players.values()) {
        await player.actor.push(
          PacketNames.gameStateNotify,
          gameStateNotify(change.state)
        );
      }
    }
  }

  async leaveGame(actor: PlaySpotActor, roomId: string): Promise<void> {
    if (roomId !== this.requireRoomId()) {
      throw new Error(`Actor requested leave for a different room. roomId=${roomId}`);
    }
    if (!isTerminal(this.match.snapshot().status)) {
      throw new Error('Game is not finished.');
    }
    actor.markForDestroyAfterRoomLeave();
    await this.context.leaveActor(actor);
  }

  private async publishWinMilestone(
    actor: PlaySpotActor,
    before: GameState,
    after: GameState
  ): Promise<void> {
    if (
      before.status === GameStatus.Won ||
      after.status !== GameStatus.Won ||
      after.winner !== actor.actorId
    ) {
      return;
    }
    const wins = actor.wins + 1;
    if (wins !== 100) {
      return;
    }
    await this.context.outbound
      .publish(
        SampleNames.playerMilestoneTopic,
        playerWinMilestoneEvent(after.roomId, actor.actorId, actor.displayName, wins)
      )
      .packetName(PacketNames.playerWinMilestoneEvent)
      .submit();
  }

  private async join(actor: PlaySpotActor, request: TicTacToeGameJoinReq): Promise<JoinGameRes> {
    const match = this.requireMatch();
    const roomId = this.requireRoomId();
    if (request.roomId !== roomId) {
      throw new Error(`Actor requested join for a different room. roomId=${request.roomId}`);
    }
    if (request.player.actorId !== actor.actorId) {
      throw new Error(`Join player '${request.player.actorId}' does not match actor '${actor.actorId}'.`);
    }
    if (request.player.level < SampleDefaults.requiredLevel) {
      throw new Error(`Player level ${request.player.level} is below required level ${SampleDefaults.requiredLevel}.`);
    }
    actor.displayName = request.player.displayName;
    actor.level = request.player.level;
    actor.wins = request.player.wins;
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
          playerJoinedNotify(
            roomId,
            result.joined.actorId,
            result.joined.actor.displayName,
            result.joined.actor.level,
            result.joined.mark,
            state
          )
        );
        await player.actor.push(PacketNames.gameStateNotify, gameStateNotify(state));
      }
    }
    return joinGameRes(state);
  }

  private requireRoomId(): string {
    return this.roomId;
  }

  private requireMatch(): TicTacToeMatchType<PlaySpotActor> {
    return this.match;
  }
}

function isTerminal(status: GameStatus): boolean {
  return status === GameStatus.Won
    || status === GameStatus.Draw
    || status === GameStatus.TurnTimedOut;
}

export { TicTacToeGameSpot };
