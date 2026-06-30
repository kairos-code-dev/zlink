import { Inject } from '@nestjs/common';
import { ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import { questMissionRouteRid } from '../Configuration/gamequest-routing';
import {
  getQuestProgressReq,
  PacketNames,
  subscribeQuestReq,
  syncQuestProgressReq
} from '../../Shared/Contracts/messages';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { PlayerSessionDirectory } from './player-session-directory';
import type {
  ZLinkMessage,
  ZLinkRouteClient,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import type {
  GetQuestProgressReq,
  GetQuestProgressRes,
  SubscribeQuestReq,
  SubscribeQuestRes,
  SyncQuestProgressReq,
  SyncQuestProgressRes
} from '../../Shared/Contracts/messages';

class GameQuestSession implements ZLinkSession {
  constructor(
    readonly context: ZLinkSessionContext,
    private readonly routes: ZLinkRouteClient,
    private readonly sessions: PlayerSessionDirectory
  ) {}

  async onConnected(context: ZLinkSessionContext): Promise<void> {
    this.sessions.add(context);
  }

  async onDisconnected(context: ZLinkSessionContext): Promise<void> {
    this.sessions.remove(context);
  }

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (dispatch.packetName === PacketNames.subscribeQuestReq) {
      const request = payload.decode<SubscribeQuestReq>(Object as never);
      this.sessions.subscribe(this.context, request.playerId);
      const response = await requestQuest<SubscribeQuestRes>(this.routes, subscribeQuestReq(request.playerId));
      await this.context.client.reply(response).submit();
      return;
    }
    if (dispatch.packetName === PacketNames.getQuestProgressReq) {
      const request = payload.decode<GetQuestProgressReq>(Object as never);
      const response = await requestQuest<GetQuestProgressRes>(this.routes, getQuestProgressReq(request.playerId));
      await this.context.client.reply(response).submit();
      return;
    }
    if (dispatch.packetName === PacketNames.syncQuestProgressReq) {
      const request = payload.decode<SyncQuestProgressReq>(Object as never);
      const response = await requestQuest<SyncQuestProgressRes>(this.routes, syncQuestProgressReq(request.playerId));
      await this.sessions.publish(request.playerId, response.updatedQuests);
      await this.context.client.reply(response).submit();
      return;
    }
    throw new Error(`Unsupported GameQuest stream packet '${dispatch.packetName}'.`);
  }
}

class GameQuestSessionFactory implements ZLinkSessionFactory<GameQuestSession> {
  constructor(
    @Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient,
    private readonly sessions: PlayerSessionDirectory
  ) {}

  create(context: ZLinkSessionContext): GameQuestSession {
    return new GameQuestSession(context, this.routes, this.sessions);
  }
}

async function requestQuest<TResponse>(routes: ZLinkRouteClient, payload: unknown): Promise<TResponse> {
  let lastError: unknown;
  for (let attempt = 0; attempt < 40; attempt += 1) {
    try {
      return await routes
        .request(SampleNames.questMissionRouteChannel, questRouteRid(payload), payload)
        .submit<TResponse>();
    } catch (error) {
      lastError = error;
      await delay(250);
    }
  }
  throw lastError instanceof Error ? lastError : new Error(String(lastError));
}

function questRouteRid(payload: unknown): string {
  if (typeof payload === 'object' && payload !== null && 'playerId' in payload) {
    return questMissionRouteRid(String((payload as { playerId: unknown }).playerId));
  }
  return 'mission-a';
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

export {
  GameQuestSession,
  GameQuestSessionFactory,
  requestQuest
};
