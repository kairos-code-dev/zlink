import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import { questMissionRouteRid, SampleNames } from '../../Shared/Configuration/sample-names';
import { GAMEQUEST_LOCATION_STORE } from '../Configuration/tokens';
import {
  JoinSessionRes,
  PacketNames,
  getQuestProgressReq
} from '../../Shared/Contracts/messages';
import type {
  JoinSessionReq
} from '../../Shared/Contracts/messages';
import type {
  ZLinkActorManager,
  ZLinkMessage,
  ZLinkRouteClient,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory,
  ZLinkLocationStore
} from '@zlink-systems/framework';
import type { GetQuestProgressRes } from '../../Shared/Contracts/messages';

class GameQuestSession implements ZLinkSession {
  private playerId: string | undefined;

  constructor(
    readonly context: ZLinkSessionContext,
    private readonly locations: ZLinkLocationStore,
    private readonly routes: ZLinkRouteClient,
    private readonly actorManager: ZLinkActorManager
  ) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (dispatch.packetName === PacketNames.joinSessionReq) {
      const request = payload.decode<JoinSessionReq>(Object as never);
      if (this.playerId !== undefined && this.playerId !== request.playerId) {
        throw new Error(`Session is already bound to player '${this.playerId}'.`);
      }
      const actorRef = (await this.locations.resolveActor({ actorId: request.playerId }))?.actorRef ??
        await this.actorManager.find(request.playerId) ??
        await this.actorManager.getOrCreate(request.playerId, SampleNames.playerActorType);
      await this.context.actors.bindOrGet(actorRef);
      this.playerId = request.playerId;
      const current = await this.getProjection(request.playerId);
      this.context.client.reply(new JoinSessionRes(current.activeQuests)).submit();
      return;
    }
    if (isPlayerPacket(dispatch.packetName)) {
      if (this.playerId === undefined) throw new Error(`JoinSessionReq is required before '${dispatch.packetName}'.`);
      const actor = this.context.actors.find(this.playerId);
      if (actor === undefined) throw new Error(`Bound player actor '${this.playerId}' was not found.`);
      await actor.relay(payload);
      return;
    }
    throw new Error(`Unsupported GameQuest packet '${dispatch.packetName}'.`);
  }

  private async getProjection(playerId: string): Promise<GetQuestProgressRes> {
    const request = getQuestProgressReq(playerId);
    return await this.routes
      .requestToNode(SampleNames.questMissionRouteChannel, questMissionRouteRid(playerId), request)
      .timeout(SampleNames.requestTimeout)
      .submit<GetQuestProgressRes>();
  }

}

class GameQuestSessionFactory implements ZLinkSessionFactory<GameQuestSession> {
  constructor(
    @Inject(GAMEQUEST_LOCATION_STORE) private readonly locations: ZLinkLocationStore,
    @Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient,
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager
  ) {}

  async create(context: ZLinkSessionContext): Promise<GameQuestSession> {
    return new GameQuestSession(
      context,
      this.locations,
      this.routes,
      this.actorManager
    );
  }
}

function isPlayerPacket(packetName: string): boolean {
  return packetName === PacketNames.getQuestProgressReq
    || packetName === PacketNames.syncQuestProgressReq
    || packetName === PacketNames.killMonsterReq
    || packetName === PacketNames.collectItemReq
    || packetName === PacketNames.completeMissionReq
    || packetName === PacketNames.enterAreaReq
    || packetName === PacketNames.unlockFeatureReq;
}

export { GameQuestSession, GameQuestSessionFactory };
