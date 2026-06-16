import { Inject } from '@nestjs/common';
import { ModuleRef } from '@nestjs/core';
import { ZLINK_ACTOR_MANAGER, ZLINK_CHANNEL_CLIENT, ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { PlayEntrySpot } from '../Spots/play-entry-spot';
import { PlayActorJoinGameHandler } from '../Spots/Handlers/play-actor-join-game-handler';
import { PlayActorPlaceMarkHandler } from '../Spots/Handlers/play-actor-place-mark-handler';
import { PlaySession } from './play-session';
import type {
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkSessionContext,
  ZLinkSessionFactory,
  ZLinkSpotManager
} from '@zlink-systems/framework';
import type { PlayActorJoinGameHandler as PlayActorJoinGameHandlerType } from '../Spots/Handlers/play-actor-join-game-handler';
import type { PlayActorPlaceMarkHandler as PlayActorPlaceMarkHandlerType } from '../Spots/Handlers/play-actor-place-mark-handler';
import type { PlaySession as PlaySessionType } from './play-session';
import type { TicTacToeActor } from '../../../../../Shared/Contracts/messages';

type PlaySessionActor = TicTacToeActor & ZLinkActor;

type PlayActorManager = {
  getOrCreate(actorId: string, actorType: string, signal?: AbortSignal): Promise<PlaySessionActor>;
};

class PlaySessionFactory implements ZLinkSessionFactory<PlaySessionType> {
  private joinGameHandler: PlayActorJoinGameHandlerType | null;
  private placeMarkHandler: PlayActorPlaceMarkHandlerType | null;

  constructor(
    private readonly actorManager: PlayActorManager,
    private readonly moduleRef: InstanceType<typeof ModuleRef>,
    private readonly apiClient: ZLinkChannelClient,
    private readonly spotManager: ZLinkSpotManager
  ) {
    this.actorManager = actorManager;
    this.moduleRef = moduleRef;
    this.apiClient = apiClient;
    this.spotManager = spotManager;
    this.joinGameHandler = null;
    this.placeMarkHandler = null;
  }

  async create(context: ZLinkSessionContext): Promise<PlaySessionType> {
    this.joinGameHandler ??= await this.moduleRef.create(PlayActorJoinGameHandler);
    this.placeMarkHandler ??= await this.moduleRef.create(PlayActorPlaceMarkHandler);
    return new PlaySession({
      apiClient: this.apiClient,
      actorManager: this.actorManager,
      entrySpot: new PlayEntrySpot(),
      joinGameHandler: this.joinGameHandler,
      spotManager: this.spotManager,
      placeMarkHandler: this.placeMarkHandler
    }, context);
  }
}

Inject(ZLINK_ACTOR_MANAGER)(PlaySessionFactory, undefined, 0);
Inject(ModuleRef)(PlaySessionFactory, undefined, 1);
Inject(ZLINK_CHANNEL_CLIENT)(PlaySessionFactory, undefined, 2);
Inject(ZLINK_SPOT_MANAGER)(PlaySessionFactory, undefined, 3);

export { PlaySessionFactory };
