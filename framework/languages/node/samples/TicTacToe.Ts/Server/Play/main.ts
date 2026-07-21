import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { ZLINK_FRAMEWORK_RUNTIME } from '@zlink-systems/nestjs';
import { closeNestRuntime, waitForShutdown } from '../runtime-support';
import { TICTACTOE_SAMPLE_CONFIG } from '../Configuration/sample-config';
import type { TicTacToeSampleConfig } from '../Configuration/sample-config';
import { createTicTacToePlayModule } from './tictactoe-play-module';
async function main(): Promise<void> {
  const TicTacToePlayModule = createTicTacToePlayModule();
  const channelApp = await NestFactory.createApplicationContext(TicTacToePlayModule, {
    logger: false,
    abortOnError: false
  });
  const config = channelApp.get<TicTacToeSampleConfig>(TICTACTOE_SAMPLE_CONFIG);
  const runtime = channelApp.get(ZLINK_FRAMEWORK_RUNTIME) as unknown as {
        spotNodeRuntime?: {
          primaryMeshNode?: {
        entrySpot(): { routingId: unknown };
            status(): { admittedPeerCount?: number };
        peers(): unknown[];
      };
    };
  };
  void logSpotPeerReady(runtime);
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.playSpotEndpoint,
    spotEndpoint: config.playSpotEndpoint,
    streamEndpoint: config.playStreamEndpoint
  })}\n`);
  await waitForShutdown();
  await closeNestRuntime(channelApp);
}

async function logSpotPeerReady(runtime: {
    spotNodeRuntime?: {
      primaryMeshNode?: {
      status(): { admittedPeerCount?: number };
    };
  };
}): Promise<void> {
  const node = runtime.spotNodeRuntime?.primaryMeshNode;
  if (node === undefined) {
    return;
  }
  const deadline = Date.now() + 3000;
  while (Date.now() < deadline) {
    if ((node.status().admittedPeerCount ?? 0) > 0) {
      process.stdout.write(`${JSON.stringify({ event: 'spotPeerReady' })}\n`);
      return;
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
  process.stderr.write('Timed out waiting for Play SpotNode peer connection.\n');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
