import { Inject, Injectable } from '@nestjs/common';
import {
  ZLINK_ACTOR_CLIENT,
  zlinkEntrySpotActorSendHandler
} from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../../Configuration/sample-names';
import { PlayerActor } from '../../Actors/player-actor';
import { PendingBingoActorDestroyRegistry } from '../../Actors/player-actor-lifecycle-handlers';
import {
  DestroyBingoActor,
  EnsurePlayerActorReq as GeneratedEnsurePlayerActorReq
} from '../../../../../../Shared/Contracts/bingo-messages.generated';
import type {
  ZLinkActorClient,
  ZLinkActorMembership,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotActorSendContext
} from '@zlink-systems/framework';

@Injectable()
class BingoEntrySpot implements ZLinkEntrySpot<PlayerActor> {
  readonly context!: ZLinkEntrySpotContext<PlayerActor>;

  constructor(
    private readonly pendingDestroys: PendingBingoActorDestroyRegistry,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient
  ) {}

  async onJoinedActor(actor: ZLinkActorMembership): Promise<void> {
    if (!this.pendingDestroys.consume(actor.actor.actorId)) {
      console.error(`bingo-lifecycle entry-joined actor=${actor.actor.actorId} destroy=false`);
      return;
    }
    const submitted = await this.actors
      .sendToActor(SampleNames.roomSpotNode, actor.actor, new DestroyBingoActor({}))
      .submit();
    if (submitted.status !== 'submitted') {
      throw new Error(`Bingo actor '${actor.actor.actorId}' destroy was not submitted: ${submitted.status}.`);
    }
  }

  async onActorJoin(
    _actorId: string,
    _request: ZLinkMessage
  ): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }

  async onCreateActor(actor: ZLinkActorMembership, createRequest: ZLinkMessage): Promise<void> {
    const request = createRequest.decode<GeneratedEnsurePlayerActorReq>();
    if (typeof request.displayName === 'string') {
      await this.actors
        .sendToActor(SampleNames.roomSpotNode, actor.actor, request)
        .submit();
    }
  }

  async onLeaveActor(actor: ZLinkActorMembership): Promise<void> {
    console.error(`bingo-lifecycle entry-leave actor=${actor.actor.actorId}`);
  }

  async onDisconnectActor(_actor: ZLinkActorMembership): Promise<void> {}

  scheduleDestroy(actor: PlayerActor): void {
    void this.context.runIoWorker(async () => true).submit().then(async () => {
      console.error(`bingo-lifecycle entry-destroy-start actor=${actor.actorId}`);
      await this.context.destroyActor(actor);
      console.error(`bingo-lifecycle entry-destroy-complete actor=${actor.actorId}`);
    });
  }
}

@zlinkEntrySpotActorSendHandler({
  actor: () => PlayerActor,
  entrySpot: () => BingoEntrySpot,
  packetName: 'DestroyBingoActor'
})
class DestroyBingoActorHandler {
  constructor(private readonly entrySpot: BingoEntrySpot) {}

  async handle(
    actor: PlayerActor,
    _context: ZLinkSpotActorSendContext,
    _message: DestroyBingoActor
  ): Promise<void> {
    this.entrySpot.scheduleDestroy(actor);
  }
}

export { BingoEntrySpot, DestroyBingoActorHandler };
