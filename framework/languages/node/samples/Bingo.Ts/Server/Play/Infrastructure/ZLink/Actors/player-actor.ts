import { ZLinkSpotActorSend } from '@zlink-systems/framework';
import type { ZLinkActor, ZLinkActorContext } from '@zlink-systems/framework';
import { LeaveFinishedBingoRoomHandler } from './player-actor-lifecycle-handlers';
import {
  BingoGameEndedNotify,
  BingoGameStartedNotify,
  BingoNumberDrawnNotify,
  BingoRewardAnnouncedNotify,
  EnsurePlayerActorReq,
  PlayerJoinedNotify
} from '../../../../../Shared/Contracts/bingo-messages.generated';

class PlayerActor implements ZLinkActor {
  readonly context!: ZLinkActorContext;
  private nextSeq = 0;

  constructor(
    readonly actorId: string,
    public displayName: string
  ) {}

  configure(): void {
    this.context.handlers.addHandler(InitializePlayerActorHandler);
    this.context.handlers.addHandler(PlayerJoinedNotificationHandler);
    this.context.handlers.addHandler(BingoGameStartedNotificationHandler);
    this.context.handlers.addHandler(BingoNumberDrawnNotificationHandler);
    this.context.handlers.addHandler(BingoGameEndedNotificationHandler);
    this.context.handlers.addHandler(BingoRewardAnnouncedNotificationHandler);
    this.context.handlers.addHandler(LeaveFinishedBingoRoomHandler);
  }

  async push(payload: unknown): Promise<void> {
    this.nextSeq += 1;
    await this.context.boundSession
      .send(payload)
      .metadata('seq', String(this.nextSeq))
      .submit();
  }
}

class InitializePlayerActorHandler {
  @ZLinkSpotActorSend('EnsurePlayerActorReq')
  async handle(actor: PlayerActor, _context: unknown, message: EnsurePlayerActorReq): Promise<void> {
    actor.displayName = message.displayName;
  }
}

class PlayerJoinedNotificationHandler {
  @ZLinkSpotActorSend('PlayerJoinedNotify')
  async handle(actor: PlayerActor, _context: unknown, message: PlayerJoinedNotify): Promise<void> {
    await actor.push(message);
  }
}

class BingoGameStartedNotificationHandler {
  @ZLinkSpotActorSend('BingoGameStartedNotify')
  async handle(actor: PlayerActor, _context: unknown, message: BingoGameStartedNotify): Promise<void> {
    await actor.push(message);
  }
}

class BingoNumberDrawnNotificationHandler {
  @ZLinkSpotActorSend('BingoNumberDrawnNotify')
  async handle(actor: PlayerActor, _context: unknown, message: BingoNumberDrawnNotify): Promise<void> {
    await actor.push(message);
  }
}

class BingoGameEndedNotificationHandler {
  @ZLinkSpotActorSend('BingoGameEndedNotify')
  async handle(actor: PlayerActor, _context: unknown, message: BingoGameEndedNotify): Promise<void> {
    await actor.push(message);
  }
}

class BingoRewardAnnouncedNotificationHandler {
  @ZLinkSpotActorSend('BingoRewardAnnouncedNotify')
  async handle(actor: PlayerActor, _context: unknown, message: BingoRewardAnnouncedNotify): Promise<void> {
    await actor.push(message);
  }
}

export {
  BingoGameEndedNotificationHandler,
  BingoGameStartedNotificationHandler,
  BingoNumberDrawnNotificationHandler,
  BingoRewardAnnouncedNotificationHandler,
  InitializePlayerActorHandler,
  PlayerJoinedNotificationHandler,
  PlayerActor
};
