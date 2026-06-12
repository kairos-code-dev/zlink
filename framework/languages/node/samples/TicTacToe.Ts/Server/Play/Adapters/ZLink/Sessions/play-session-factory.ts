const { Inject } = require('@nestjs/common');
const { ModuleRef } = require('@nestjs/core');
const { ZLINK_ACTOR_MANAGER } = require('../../../../../../../../packages/nestjs/dist');
const { PlayEntrySpot } = require('../Spots/play-entry-spot');
const { PlayActorPlaceMarkHandler } = require('../Spots/Handlers/play-actor-place-mark-handler');
const { PlaySession } = require('./play-session');
import type {
  ZLinkChannelClient,
  ZLinkSessionContext,
  ZLinkSessionFactory
} from '../../../../../../../packages/framework/dist';
import type { PlayEntrySpot as PlayEntrySpotType } from '../Spots/play-entry-spot';
import type { PlayActorPlaceMarkHandler as PlayActorPlaceMarkHandlerType } from '../Spots/Handlers/play-actor-place-mark-handler';
import type { PlaySession as PlaySessionType } from './play-session';

type PlayActorManager = {
  getOrCreate(actorId: string, actorType: string, signal?: AbortSignal): Promise<unknown>;
};

class PlaySessionFactory implements ZLinkSessionFactory<PlaySessionType> {
  private placeMarkHandler: PlayActorPlaceMarkHandlerType | null;

  constructor(
    private readonly actorManager: PlayActorManager,
    private readonly entrySpot: PlayEntrySpotType,
    private readonly moduleRef: InstanceType<typeof ModuleRef>,
    private readonly apiClient: ZLinkChannelClient
  ) {
    this.actorManager = actorManager;
    this.entrySpot = entrySpot;
    this.moduleRef = moduleRef;
    this.apiClient = apiClient;
    this.placeMarkHandler = null;
  }

  async create(context: ZLinkSessionContext): Promise<PlaySessionType> {
    this.placeMarkHandler ??= await this.moduleRef.create(PlayActorPlaceMarkHandler);
    return new PlaySession({
      apiClient: this.apiClient,
      actorManager: this.actorManager,
      entrySpot: this.entrySpot,
      placeMarkHandler: this.placeMarkHandler
    }, context);
  }
}

Inject(ZLINK_ACTOR_MANAGER)(PlaySessionFactory, undefined, 0);
Inject(PlayEntrySpot)(PlaySessionFactory, undefined, 1);
Inject(ModuleRef)(PlaySessionFactory, undefined, 2);
Inject('TICTACTOE_API_CHANNEL_CLIENT')(PlaySessionFactory, undefined, 3);

export { PlaySessionFactory };
