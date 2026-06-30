import 'reflect-metadata';
import { ZLinkHttpClient } from '@zlink-systems/http-client';
import * as connector from '@zlink-systems/stream-connector';
import { loadSampleConfig } from './Configuration/sample-config';
import { GameQuestClientScenario } from './gamequest-client-scenario';
import { SampleTimings } from '../Shared/Configuration/sample-names';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const apiA = ZLinkHttpClient.create(config.apiAHttpUrl).timeout(SampleTimings.httpTimeout).build();
  const apiB = ZLinkHttpClient.create(config.apiBHttpUrl).timeout(SampleTimings.httpTimeout).build();
  const apiAStream = createStream(config.apiAStreamEndpoint);
  const apiBStream = createStream(config.apiBStreamEndpoint);
  try {
    await new GameQuestClientScenario().run(
      apiA,
      apiB,
      apiAStream,
      apiBStream,
      AbortSignal.timeout(SampleTimings.workflowTimeout)
    );
  } finally {
    await Promise.allSettled([
      apiA.close(),
      apiB.close(),
      apiAStream.close(),
      apiBStream.close()
    ]);
  }
  console.log('gamequest=completed');
  console.log('PASS GameQuest.Ts');
}

function createStream(endpoint: string): ZlinkStreamConnector {
  return connector.zlinkStreamConnectorFactory.create({
    endpoint,
    codec: connector.zlinkStreamJsonCodec,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleTimings.requestTimeout,
    waitTimeoutMs: SampleTimings.workflowTimeout,
    heartbeat: { enabled: false }
  });
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
