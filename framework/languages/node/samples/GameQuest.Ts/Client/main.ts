import { ZLinkHttpClient } from '@zlink-systems/http-client';
import * as connector from '@zlink-systems/stream-connector';
import { SampleNames } from '../Shared/Configuration/sample-names';
import { GameQuestClientScenario } from './gamequest-client-scenario';
import { loadSampleConfig } from './Configuration/sample-config';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const apiA = ZLinkHttpClient.create(config.apiAHttpUrl).timeout(SampleNames.clientTimeout).build();
  const apiB = ZLinkHttpClient.create(config.apiBHttpUrl).timeout(SampleNames.clientTimeout).build();
  const missionA = ZLinkHttpClient.create(config.missionAHttpUrl).timeout(SampleNames.clientTimeout).build();
  const missionB = ZLinkHttpClient.create(config.missionBHttpUrl).timeout(SampleNames.clientTimeout).build();
  const apiAStream = createClient(config.apiAStreamEndpoint, 'api-a');
  const apiBStream = createClient(config.apiBStreamEndpoint, 'api-b');
  try {
    await new GameQuestClientScenario().run(
      apiA,
      apiB,
      missionA,
      missionB,
      apiAStream,
      apiBStream
    );
  } finally {
    await Promise.allSettled([
      apiAStream.close(),
      apiBStream.close(),
      apiA.close(),
      apiB.close(),
      missionA.close(),
      missionB.close()
    ]);
  }
  console.log('gamequest=completed');
  console.log('PASS GameQuest.Ts');
}

function createClient(endpoint: string, name: string): ZlinkStreamConnector {
  const client = connector.zlinkStreamConnectorFactory.create({
    endpoint,
    codec: connector.zlinkStreamJsonCodec,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleNames.requestTimeout,
    waitTimeoutMs: SampleNames.clientTimeout,
    heartbeat: { enabled: false }
  });
  client.observeInbound((observation) => {
    console.log(
      `stream-inbound sample=GameQuest client=${name} kind=${observation.kind} ` +
      `name=${observation.name} seq=${observation.requestSeq?.toString() ?? '-'} ` +
      `bytes=${observation.payloadLength}`
    );
  });
  return client;
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
