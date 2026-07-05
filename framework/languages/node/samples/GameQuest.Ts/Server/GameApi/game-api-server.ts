import http from 'node:http';
import { QuestProgressStore } from '../Shared/Store/quest-progress-store';
import type { INestApplicationContext } from '@nestjs/common';
import type { GameQuestServerConfig } from '../Configuration/sample-config';
import type { GetGameplaySnapshotReq, GameQuestServerAssertRes } from '../../Shared/Contracts/messages';

function startGameApiServer(
  app: INestApplicationContext,
  config: GameQuestServerConfig,
  instanceId: 'api-a' | 'api-b'
): Promise<http.Server> {
  const store = app.get(QuestProgressStore, { strict: false });
  const base = new URL(instanceId === 'api-a' ? config.apiAHttpUrl : config.apiBHttpUrl);
  const server = http.createServer(async (request, response) => {
    try {
      if (request.method === 'GET' && request.url === '/health') {
        sendJson(response, 200, { ready: true, role: instanceId });
        return;
      }
      if (request.method === 'POST' && request.url === '/combat/kill') {
        store.killWithoutPublish('player-alice');
        sendJson(response, 200, { accepted: true });
        return;
      }
      if (request.method === 'POST' && request.url === '/internal/snapshot') {
        const body = await readJson<GetGameplaySnapshotReq>(request);
        sendJson(response, 200, store.readGameplaySnapshot(body.playerId));
        return;
      }
      const progressMatch = request.url?.match(/^\/quest\/progress\/([^/]+)$/);
      if (request.method === 'GET' && progressMatch !== undefined && progressMatch !== null) {
        sendJson(response, 200, { activeQuests: store.readProjection(decodeURIComponent(progressMatch[1])) });
        return;
      }
      const deleteMatch = request.url?.match(/^\/self-check\/projection\/([^/]+)\/([^/]+)\/delete$/);
      if (request.method === 'POST' && deleteMatch !== undefined && deleteMatch !== null) {
        store.deleteProjection(decodeURIComponent(deleteMatch[1]), decodeURIComponent(deleteMatch[2]));
        sendJson(response, 200, { deleted: true });
        return;
      }
      const rebuildMatch = request.url?.match(/^\/self-check\/projection\/([^/]+)\/([^/]+)\/rebuild$/);
      if (request.method === 'POST' && rebuildMatch !== undefined && rebuildMatch !== null) {
        sendJson(response, 200, store.rebuildProjection(decodeURIComponent(rebuildMatch[1]), decodeURIComponent(rebuildMatch[2])));
        return;
      }
      const missedMatch = request.url?.match(/^\/self-check\/gameplay\/kill-without-publish\/([^/]+)$/);
      if (request.method === 'POST' && missedMatch !== undefined && missedMatch !== null) {
        store.killWithoutPublish(decodeURIComponent(missedMatch[1]));
        sendJson(response, 200, { accepted: true });
        return;
      }
      if (request.method === 'POST' && request.url === '/self-check/assert') {
        sendJson(response, 200, store.assertServerEvidence() satisfies GameQuestServerAssertRes);
        return;
      }
      sendJson(response, 404, { error: 'not-found' });
    } catch (error) {
      sendJson(response, 500, { error: error instanceof Error ? error.message : String(error) });
    }
  });

  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(Number(base.port), base.hostname, () => {
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
        const text = Buffer.concat(chunks).toString('utf8');
        resolve(JSON.parse(text.length === 0 ? '{}' : text) as T);
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

export { startGameApiServer };
