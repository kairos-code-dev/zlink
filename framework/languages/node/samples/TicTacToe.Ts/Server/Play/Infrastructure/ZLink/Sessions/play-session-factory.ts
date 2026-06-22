import { Inject } from '@nestjs/common';
import { ModuleRef } from '@nestjs/core';
import { ZLINK_ACTOR_MANAGER, ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { MilestoneObserverRegistry, PlayEntrySpot } from '../Spots/play-entry-spot';
import { PlayActorJoinGameHandler } from '../Spots/Handlers/play-actor-join-game-handler';
import { PlaySession } from './play-session';
import type {
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkSessionContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import type { PlayActorJoinGameHandler as PlayActorJoinGameHandlerType } from '../Spots/Handlers/play-actor-join-game-handler';
import type { PlaySession as PlaySessionType } from './play-session';
import type { TicTacToeActor } from '../../../../../Shared/Contracts/messages';

type PlaySessionActor = TicTacToeActor & ZLinkActor;

type PlayActorManager = {
  getOrCreate(actorId: string, actorType: string, signal?: AbortSignal): Promise<PlaySessionActor>;
};

class PlaySessionFactory implements ZLinkSessionFactory<PlaySessionType> {
  private joinGameHandler: PlayActorJoinGameHandlerType | null;

  constructor(
    private readonly actorManager: PlayActorManager,
    private readonly moduleRef: InstanceType<typeof ModuleRef>,
    private readonly apiClient: ZLinkChannelClient,
    private readonly milestoneObservers: MilestoneObserverRegistry
  ) {
    this.actorManager = actorManager;
    this.moduleRef = moduleRef;
    this.apiClient = apiClient;
    this.joinGameHandler = null;
  }

  async create(context: ZLinkSessionContext): Promise<PlaySessionType> {
    this.joinGameHandler ??= await this.moduleRef.create(PlayActorJoinGameHandler);
    return new PlaySession({
      apiClient: this.apiClient,
      actorManager: this.actorManager,
      entrySpot: new PlayEntrySpot(this.milestoneObservers),
      joinGameHandler: this.joinGameHandler
    }, context);
  }
}

Inject(ZLINK_ACTOR_MANAGER)(PlaySessionFactory, undefined, 0);
Inject(ModuleRef)(PlaySessionFactory, undefined, 1);
Inject(ZLINK_CHANNEL_CLIENT)(PlaySessionFactory, undefined, 2);
Inject(MilestoneObserverRegistry)(PlaySessionFactory, undefined, 3);

export { PlaySessionFactory };
