import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { MilestoneObserverRegistry, PlayEntrySpot } from '../Spots/EntrySpot/play-entry-spot';
import { PlaySession } from './play-session';
import type {
  ActorRef,
  ZLinkChannelClient,
  ZLinkSessionContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import type { PlaySession as PlaySessionType } from './play-session';
type PlayActorManager = {
  getOrCreate(actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
};

class PlaySessionFactory implements ZLinkSessionFactory<PlaySessionType> {
  constructor(
    private readonly actorManager: PlayActorManager,
    private readonly apiClient: ZLinkChannelClient,
    private readonly milestoneObservers: MilestoneObserverRegistry
  ) {
    this.actorManager = actorManager;
    this.apiClient = apiClient;
  }

  async create(context: ZLinkSessionContext): Promise<PlaySessionType> {
    return new PlaySession({
      apiClient: this.apiClient,
      actorManager: this.actorManager,
      entrySpot: new PlayEntrySpot(this.milestoneObservers)
    }, context);
  }
}

Inject(ZLINK_ACTOR_MANAGER)(PlaySessionFactory, undefined, 0);
Inject(ZLINK_CHANNEL_CLIENT)(PlaySessionFactory, undefined, 1);
Inject(MilestoneObserverRegistry)(PlaySessionFactory, undefined, 2);

export { PlaySessionFactory };
