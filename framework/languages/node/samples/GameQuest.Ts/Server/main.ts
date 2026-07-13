import 'reflect-metadata';
import http from 'node:http';
import { NestFactory } from '@nestjs/core';
import { loadSampleConfig } from './Configuration/sample-config';
import { createGameApiModule } from './GameApi/game-api-module';
import { startGameApiServer } from './GameApi/game-api-server';
import { createQuestMissionModule } from './QuestMission/gamequest-quest-module';
import { RegistryModule } from './Registry/registry-module';
import { QuestEventStore } from './Shared/Store/quest-progress-store';
import { PlayerQuestSpotProvisioner } from './QuestMission/Infrastructure/ZLink/player-quest-spot-provisioner';
import { GAMEQUEST_LOCATION_STORE } from './Configuration/tokens';
import type { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const role = readOption('--role') ?? process.env.GAMEQUEST_ROLE ?? 'api-a';
  if (role !== 'api-a' && role !== 'api-b' && role !== 'mission-a' && role !== 'mission-b') {
    throw new Error(`Unknown GameQuest role '${role}'.`);
  }
  void RegistryModule;

  const isApi = role === 'api-a' || role === 'api-b';
  if (isApi) process.env.GAMEQUEST_API_NAME = role;
  else process.env.GAMEQUEST_MISSION_NAME = role;
  const app = await NestFactory.createApplicationContext(
    isApi ? createGameApiModule(config, role) : createQuestMissionModule(config, role), {
    logger: false,
    abortOnError: true
  });

  const httpServer = isApi
    ? await startGameApiServer(app, config, role)
    : await startMissionSelfCheckServer(config, role, app.get(PlayerQuestSpotProvisioner));

  process.stdout.write(`${JSON.stringify({ event: 'ready', role })}\n`);
  await waitForShutdown();
  await new Promise<void>((resolve) => httpServer.close(() => resolve()));
  await closeNestRuntime(app);
  await app.get<ZLinkRedisLocationStore>(GAMEQUEST_LOCATION_STORE).dispose();
}

function readOption(name: string): string | undefined {
  const index = process.argv.indexOf(name);
  return index >= 0 && index + 1 < process.argv.length ? process.argv[index + 1] : undefined;
}

async function closeNestRuntime(container: { close(): Promise<void> }): Promise<void> {
  try {
    await container.close();
  } catch (error) {
    const candidate = error as { name?: string; code?: number };
    if (candidate.name === 'CloseError' && (candidate.code === 0 || candidate.code === 401)) {
      return;
    }
    throw error;
  }
}

function waitForShutdown(): Promise<void> {
  return new Promise<void>((resolve) => {
    const keepAlive = setInterval(() => undefined, 1000);
    const stop = () => {
      clearInterval(keepAlive);
      resolve();
    };
    process.once('SIGINT', stop);
    process.once('SIGTERM', stop);
  });
}

function startMissionSelfCheckServer(
  config: ReturnType<typeof loadSampleConfig>,
  role: 'mission-a' | 'mission-b',
  playerQuests: PlayerQuestSpotProvisioner
): Promise<http.Server> {
  const store = new QuestEventStore(config.workDir);
  const missionUrl = role === 'mission-a'
    ? process.env.GAMEQUEST_MISSION_A_HTTP ?? 'http://127.0.0.1:31213'
    : process.env.GAMEQUEST_MISSION_B_HTTP ?? 'http://127.0.0.1:31214';
  const url = new URL(missionUrl);
  const server = http.createServer(async (request, response) => {
    const ownerMatch = request.url?.match(/^\/self-check\/owner\/([^/]+)\/close$/);
    if (request.method === 'GET' && request.url === '/health') {
      sendJson(response, 200, { ready: true, role });
      return;
    }
    if (request.method === 'GET' && request.url === '/self-check/events') {
      sendJson(response, 200, store.readQuestEventNames());
      return;
    }
    if (request.method === 'POST' && ownerMatch !== undefined && ownerMatch !== null) {
      const playerId = decodeURIComponent(ownerMatch[1]);
      const closed = await playerQuests.deactivate(playerId);
      if (closed) store.closeOwner(playerId, role);
      sendJson(response, 200, { closed });
      return;
    }
    sendJson(response, 404, { error: 'not-found' });
  });
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(Number(url.port), url.hostname, () => {
      server.off('error', reject);
      resolve(server);
    });
  });
}

function sendJson(response: http.ServerResponse, statusCode: number, body: unknown): void {
  response.writeHead(statusCode, { 'content-type': 'application/json' });
  response.end(JSON.stringify(body));
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
