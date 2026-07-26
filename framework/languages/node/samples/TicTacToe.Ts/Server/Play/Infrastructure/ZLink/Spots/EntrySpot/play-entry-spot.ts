import { Inject, Injectable } from '@nestjs/common';
import {
  ZLINK_ACTOR_CLIENT,
  zlinkEntrySpotActorSendHandler
} from '@zlink-systems/nestjs';
import { winMilestoneNotify } from '../../../../../../Shared/Contracts/messages';
import { PlayerWinMilestoneEventHandler } from './Handlers/player-win-milestone-event-handler';
import { SampleNames } from '../../../../../Configuration/sample-settings';
import {
  DeliverPlayNotification,
  InitializePlayActor,
  PlayActor
} from '../../Actors/play-actor';
import type {
  ActorRef,
  ZLinkActorClient,
  ZLinkActorMembership,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage
} from '@zlink-systems/framework';
import type {
  PlayerWinMilestoneEvent,
  TicTacToeActor
} from '../../../../../../Shared/Contracts/messages';

@Injectable()
class MilestoneObserverRegistry {
  private readonly actors = new Map<string, ActorRef>();
  private readonly subscriptions = new Set<string>();

  constructor(@Inject(ZLINK_ACTOR_CLIENT) private readonly actorClient: ZLinkActorClient) {}

  track(actor: ZLinkActorMembership): void {
    this.actors.set(actor.actor.actorId, actor.actor);
  }

  subscribe(actorId: string): void {
    this.subscriptions.add(actorId);
  }

  remove(actorId: string): void {
    this.actors.delete(actorId);
    this.subscriptions.delete(actorId);
  }

  async notify(event: PlayerWinMilestoneEvent): Promise<void> {
    const payload = winMilestoneNotify(event);
    for (const actorId of this.subscriptions) {
      const actor = this.actors.get(actorId);
      if (actor !== undefined) {
        await this.actorClient
          .sendToActor(actor.actorId, new DeliverPlayNotification(payload))
          .submit();
      }
    }
  }
}

@Injectable()
class PendingActorDestroyRegistry {
  private readonly actors = new Set<string>();

  mark(actorId: string): void {
    this.actors.add(actorId);
  }

  consume(actorId: string): boolean {
    return this.actors.delete(actorId);
  }
}

class DestroyPlayActor {}

@Injectable()
class PlayEntrySpot implements ZLinkEntrySpot<PlayActor> {
  readonly context!: ZLinkEntrySpotContext<PlayActor>;

  constructor(
    private readonly milestoneObservers: MilestoneObserverRegistry,
    private readonly pendingDestroys: PendingActorDestroyRegistry,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actorClient: ZLinkActorClient
  ) {}

  configure(): void {
    this.context.handlers.addSubscribe(
      PlayerWinMilestoneEventHandler,
      SampleNames.playerMilestoneChannel,
      SampleNames.playerMilestoneTopic
    );
  }

  async onActorJoin(_actorId: string, _request: ZLinkMessage): Promise<{ accepted: boolean }> {
    return { accepted: true };
  }

  async notifyMilestone(event: PlayerWinMilestoneEvent): Promise<void> {
    await this.milestoneObservers.notify(event);
  }

  async onDisconnectActor(actor: ZLinkActorMembership): Promise<void> {
    this.milestoneObservers.remove(actor.actor.actorId);
  }

  async onCreateActor(actor: ZLinkActorMembership, createRequest: ZLinkMessage): Promise<void> {
    const player = createRequest.decode<Partial<TicTacToeActor>>(Object as never);
    await this.actorClient.sendToActor(
      actor.actor.actorId,
      new InitializePlayActor(
        typeof player.displayName === 'string' ? player.displayName : actor.actor.actorId,
        typeof player.level === 'number' ? player.level : 0,
        typeof player.wins === 'number' ? player.wins : 0
      )
    ).submit();
    this.milestoneObservers.track(actor);
  }

  async onJoinedActor(actor: ZLinkActorMembership): Promise<void> {
    this.milestoneObservers.track(actor);
    if (this.pendingDestroys.consume(actor.actor.actorId)) {
      const submitted = await this.actorClient
        .sendToActor(actor.actor.actorId, new DestroyPlayActor())
        .submit();
      if (submitted.status !== 'submitted') {
        throw new Error(
          `Actor '${actor.actor.actorId}' destroy command was not submitted: ${submitted.status}.`
        );
      }
    }
  }

  async onLeaveActor(actor: ZLinkActorMembership): Promise<void> {
    console.log(`entry spot: actor left. actor=${actor.actor.actorId}`);
    this.milestoneObservers.remove(actor.actor.actorId);
  }

  scheduleDestroy(actor: PlayActor): void {
    void this.context.runIoWorker(async () => true).submit().then(async () => {
      console.log(`entry spot: actor destroy started. actor=${actor.actorId}`);
      await this.context.destroyActor(actor);
      console.log(`entry spot: actor destroyed. actor=${actor.actorId}`);
    });
  }
}

@zlinkEntrySpotActorSendHandler({
  entrySpot: () => PlayEntrySpot,
  actor: () => PlayActor,
  packetName: 'DestroyPlayActor'
})
class DestroyPlayActorHandler {
  constructor(private readonly entrySpot: PlayEntrySpot) {}

  async handle(actor: PlayActor): Promise<void> {
    this.entrySpot.scheduleDestroy(actor);
  }
}

export {
  DestroyPlayActorHandler,
  MilestoneObserverRegistry,
  PendingActorDestroyRegistry,
  PlayEntrySpot
};
