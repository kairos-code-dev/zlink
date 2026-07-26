import type {
  ZLinkActor,
  ZLinkActorContext
} from '@zlink-systems/framework';
import { ZLinkSpotActorSend } from '@zlink-systems/framework';
import type { TicTacToeActor } from '../../../../../Shared/Contracts/messages';
import {
  GameStateNotify,
  PlayerJoinedNotify,
  WinMilestoneNotify
} from '../../../../../Shared/Contracts/messages';
import { PlayActorJoinGameHandler } from '../Spots/EntrySpot/Handlers/play-actor-join-game-handler';
import { PlayActorObserveMilestoneHandler } from '../Spots/EntrySpot/Handlers/play-actor-observe-milestone-handler';
import { PlayActorLeaveGameHandler } from '../Spots/TicTacToeGameSpot/Handlers/play-actor-leave-game-handler';
import { PlayActorPlaceMarkHandler } from '../Spots/TicTacToeGameSpot/Handlers/play-actor-place-mark-handler';

type PlayNotification = PlayerJoinedNotify | GameStateNotify | WinMilestoneNotify;

class InitializePlayActor {
  constructor(
    readonly displayName: string,
    readonly level: number,
    readonly wins: number
  ) {}
}

class DeliverPlayNotification {
  readonly kind: 'gameState' | 'playerJoined' | 'winMilestone';

  constructor(readonly payload: PlayNotification) {
    this.kind = payload instanceof PlayerJoinedNotify
      ? 'playerJoined'
      : payload instanceof WinMilestoneNotify
        ? 'winMilestone'
        : 'gameState';
  }
}

class PlayActor implements ZLinkActor, TicTacToeActor {
  readonly actorId: string;
  readonly context!: ZLinkActorContext;
  displayName: string;
  level: number;
  wins: number;
  roomId?: string;
  private nextSeq: number;

  constructor(actorId: string, displayName: string, context?: ZLinkActorContext, level = 0, wins = 0) {
    this.actorId = actorId;
    if (context !== undefined) {
      Object.defineProperty(this, 'context', {
        configurable: true,
        enumerable: true,
        value: context
      });
    }
    this.displayName = displayName;
    this.level = level;
    this.wins = wins;
    this.nextSeq = 0;
  }

  configure(): void {
    this.context.handlers.addHandler(PlayActorJoinGameHandler);
    this.context.handlers.addHandler(PlayActorObserveMilestoneHandler);
    this.context.handlers.addHandler(PlayActorPlaceMarkHandler);
    this.context.handlers.addHandler(PlayActorLeaveGameHandler);
    this.context.handlers.addHandler(InitializePlayActorHandler);
    this.context.handlers.addHandler(DeliverPlayNotificationHandler);
  }

  async push(payload: PlayNotification): Promise<void> {
    this.nextSeq += 1;
    await this.context.boundSession
      .send(payload)
      .metadata('seq', String(this.nextSeq))
      .submit();
  }
}

class InitializePlayActorHandler {
  @ZLinkSpotActorSend('InitializePlayActor')
  async handle(actor: PlayActor, _context: unknown, message: InitializePlayActor): Promise<void> {
    actor.displayName = message.displayName;
    actor.level = message.level;
    actor.wins = message.wins;
  }
}

class DeliverPlayNotificationHandler {
  @ZLinkSpotActorSend('DeliverPlayNotification')
  async handle(actor: PlayActor, _context: unknown, message: DeliverPlayNotification): Promise<void> {
    const payload = message.payload;
    if (message.kind === 'playerJoined') {
      const value = payload as PlayerJoinedNotify;
      await actor.push(new PlayerJoinedNotify(
        value.roomId,
        value.actorId,
        value.displayName,
        value.level,
        value.mark,
        value.state
      ));
      return;
    }
    if (message.kind === 'winMilestone') {
      const value = payload as WinMilestoneNotify;
      await actor.push(new WinMilestoneNotify(
        value.roomId,
        value.actorId,
        value.displayName,
        value.wins
      ));
      return;
    }
    const value = payload as GameStateNotify;
    await actor.push(new GameStateNotify(value.state));
  }
}

export {
  DeliverPlayNotification,
  DeliverPlayNotificationHandler,
  InitializePlayActor,
  InitializePlayActorHandler,
  PlayActor
};
