import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import { questMissionRouteRid, SampleNames } from '../../Shared/Configuration/sample-names';
import { GAMEQUEST_LOCATION_STORE } from '../Configuration/tokens';
import {
  JoinSessionRes,
  PacketNames,
  getQuestProgressReq
} from '../../Shared/Contracts/messages';
import {
  ZLinkPacket,
  type ZLinkActorManager,
  type ZLinkMessage,
  type ZLinkRouteClient,
  type ZLinkSession,
  type ZLinkSessionContext,
  type ZLinkSessionDispatchContext,
  type ZLinkSessionFactory,
  type ZLinkLocationStore
} from '@zlink-systems/framework';
import type {
  JoinSessionReq
} from '../../Shared/Contracts/messages';
import type { GetQuestProgressRes } from '../../Shared/Contracts/messages';

class GameQuestSession implements ZLinkSession {
  constructor(readonly context: ZLinkSessionContext) {
    context.handlers.addHandler(JoinSessionHandler);
  }

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (await this.context.handlers.tryHandle(dispatch, payload)) return;
    const actor = this.context.actors.bound.length === 1 ? this.context.actors.bound[0] : undefined;
    if (actor === undefined) throw new Error(`JoinSessionReq is required before '${dispatch.packetName}'.`);
    await actor.relay(payload);
  }
}

@Injectable()
@ZLinkPacket(PacketNames.joinSessionReq)
class JoinSessionHandler {
  constructor(
    @Inject(GAMEQUEST_LOCATION_STORE) private readonly locations: ZLinkLocationStore,
    @Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient,
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager
  ) {}

  async handle(
    context: ZLinkSessionContext,
    _dispatch: ZLinkSessionDispatchContext,
    payload: ZLinkMessage
  ): Promise<void> {
    const request = payload.decode<JoinSessionReq>(Object as never);
    const existing = context.actors.bound.at(0);
    if (existing !== undefined && existing.actorId !== request.playerId) {
      throw new Error(`Session is already bound to player '${existing.actorId}'.`);
    }
    const actorRef = (await this.locations.resolveActor({ actorId: request.playerId }))?.actorRef ??
      await this.actorManager.find(request.playerId) ??
      await this.actorManager.getOrCreate(request.playerId, SampleNames.playerActorType);
    await context.actors.bindOrGet(actorRef);
    const current = await this.getProjection(request.playerId);
    context.client.reply(new JoinSessionRes(current.activeQuests)).submit();
  }

  private async getProjection(playerId: string): Promise<GetQuestProgressRes> {
    return await this.routes
      .requestToNode(SampleNames.questMissionRouteChannel, questMissionRouteRid(playerId), getQuestProgressReq(playerId))
      .timeout(SampleNames.requestTimeout)
      .submit<GetQuestProgressRes>();
  }
}

class GameQuestSessionFactory implements ZLinkSessionFactory<GameQuestSession> {
  async create(context: ZLinkSessionContext): Promise<GameQuestSession> {
    return new GameQuestSession(context);
  }
}

export { GameQuestSession, GameQuestSessionFactory, JoinSessionHandler };
