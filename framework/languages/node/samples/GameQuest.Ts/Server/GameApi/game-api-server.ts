import http from 'node:http';
import { ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import {
  deleteQuestProjectionReq,
  gameQuestServerAssertReq,
  getGameplaySnapshotReq,
  getQuestProgressReq,
  rebuildQuestProjectionReq,
  syncQuestProgressReq,
  killMonsterReq
} from '../../Shared/Contracts/messages';
import { PlayerSessionDirectory } from './player-session-directory';
import { GameplayActionService } from './Application/gameplay-action-service';
import { GameplayEventPublisher } from './Infrastructure/ZLink/gameplay-event-publisher';
import type { INestApplicationContext } from '@nestjs/common';
import type { ZLinkRouteClient } from '@zlink-systems/framework';
import type {
  CollectItemReq,
  CompleteMissionReq,
  EnterAreaReq,
  EventRes,
  GameQuestServerAssertRes,
  GetGameplaySnapshotReq,
  GetGameplaySnapshotRes,
  GetQuestProgressRes,
  KillMonsterReq,
  QuestProgress,
  SyncQuestProgressRes,
  UnlockFeatureReq
} from '../../Shared/Contracts/messages';
import type { GameQuestServerConfig } from '../Configuration/sample-config';

function startGameApi(
  app: INestApplicationContext,
  config: GameQuestServerConfig,
  instanceId: 'api-a' | 'api-b'
): Promise<http.Server> {
  const routes = app.get(ZLINK_ROUTE_CLIENT, { strict: false }) as ZLinkRouteClient;
  const sessions = app.get(PlayerSessionDirectory, { strict: false });
  const publisher = new GameplayEventPublisher(routes);
  const gameplay = new GameplayActionService(publisher, sessions);
  const server = http.createServer(async (request, response) => {
    try {
      const url = new URL(request.url ?? '/', 'http://127.0.0.1');
      if (request.method === 'GET' && url.pathname === '/health') {
        sendJson(response, 200, { ready: true, role: instanceId });
        return;
      }
      if (request.method === 'POST' && url.pathname === '/combat/kill') {
        const body = await readJson<KillMonsterReq>(request);
        const result = await gameplay.killMonster(body);
        sendJson(response, 200, result);
        return;
      }
      if (request.method === 'POST' && url.pathname === '/inventory/collect') {
        const body = await readJson<CollectItemReq>(request);
        const result = await gameplay.collectItem(body);
        sendJson(response, 200, result);
        return;
      }
      if (request.method === 'POST' && url.pathname === '/mission/complete') {
        const body = await readJson<CompleteMissionReq>(request);
        const result = await gameplay.completeMission(body);
        sendJson(response, 200, result);
        return;
      }
      if (request.method === 'POST' && url.pathname === '/world/enter') {
        const body = await readJson<EnterAreaReq>(request);
        const result = await gameplay.enterArea(body);
        sendJson(response, 200, result);
        return;
      }
      if (request.method === 'POST' && url.pathname === '/feature/unlock') {
        const body = await readJson<UnlockFeatureReq>(request);
        const result = await gameplay.unlockFeature(body);
        sendJson(response, 200, result);
        return;
      }
      const progressMatch = /^\/quest\/progress\/([^/]+)$/.exec(url.pathname);
      if (request.method === 'GET' && progressMatch !== null) {
        sendJson(response, 200, await publisher.request<GetQuestProgressRes>(getQuestProgressReq(decodeURIComponent(progressMatch[1]))));
        return;
      }
      if (request.method === 'POST' && url.pathname === '/internal/snapshot') {
        const body = await readJson<GetGameplaySnapshotReq>(request);
        sendJson(response, 200, await publisher.request<GetGameplaySnapshotRes>(getGameplaySnapshotReq(body.playerId)));
        return;
      }
      const killWithoutPublish = /^\/self-check\/gameplay\/kill-without-publish\/([^/]+)$/.exec(url.pathname);
      if (request.method === 'POST' && killWithoutPublish !== null) {
        const playerId = decodeURIComponent(killWithoutPublish[1]);
        sendJson(response, 200, await publisher.request<EventRes>(killMonsterReq(playerId, 'wolf', 'forest', 'sync-hidden-kill')));
        return;
      }
      const syncMatch = /^\/self-check\/sync\/([^/]+)$/.exec(url.pathname);
      if (request.method === 'POST' && syncMatch !== null) {
        const playerId = decodeURIComponent(syncMatch[1]);
        const result = await publisher.request<SyncQuestProgressRes>(syncQuestProgressReq(playerId));
        await sessions.publish(playerId, result.updatedQuests);
        sendJson(response, 200, result);
        return;
      }
      const deleteMatch = /^\/self-check\/projection\/([^/]+)\/([^/]+)\/delete$/.exec(url.pathname);
      if (request.method === 'POST' && deleteMatch !== null) {
        sendJson(response, 200, await publisher.request<QuestProgress | undefined>(
          deleteQuestProjectionReq(decodeURIComponent(deleteMatch[1]), decodeURIComponent(deleteMatch[2]))
        ));
        return;
      }
      const rebuildMatch = /^\/self-check\/projection\/([^/]+)\/([^/]+)\/rebuild$/.exec(url.pathname);
      if (request.method === 'POST' && rebuildMatch !== null) {
        sendJson(response, 200, await publisher.request<QuestProgress>(
          rebuildQuestProjectionReq(decodeURIComponent(rebuildMatch[1]), decodeURIComponent(rebuildMatch[2]))
        ));
        return;
      }
      if (request.method === 'POST' && url.pathname === '/self-check/assert') {
        sendJson(response, 200, await publisher.request<GameQuestServerAssertRes>(gameQuestServerAssertReq()));
        return;
      }
      sendJson(response, 404, { error: 'not-found' });
    } catch (error) {
      console.error(`gamequest ${instanceId}: ${error instanceof Error ? error.stack ?? error.message : String(error)}`);
      sendJson(response, 500, { error: error instanceof Error ? error.message : String(error) });
    }
  });

  const url = new URL(instanceId === 'api-a' ? config.apiAHttpUrl : config.apiBHttpUrl);
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(Number(url.port), url.hostname, () => {
      server.off('error', reject);
      resolve(server);
    });
  });
}

function readJson<T>(request: http.IncomingMessage): Promise<T> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    request.on('data', (chunk) => chunks.push(Buffer.from(chunk)));
    request.on('error', reject);
    request.on('end', () => {
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString('utf8')) as T);
      } catch (error) {
        reject(error);
      }
    });
  });
}

function sendJson(response: http.ServerResponse, statusCode: number, body: unknown): void {
  response.writeHead(statusCode, { 'content-type': 'application/json' });
  response.end(JSON.stringify(body));
}

export {
  startGameApi
};
