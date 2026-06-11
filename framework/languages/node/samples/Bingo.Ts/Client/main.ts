const connector = require('../../../../packages/stream-connector/dist');
const { bingoProtobufCodec } = require('../Shared/Contracts/protobuf-codec');
import { BingoClientScenario } from './bingo-client-scenario';
import type { ZlinkStreamConnector } from '../../../packages/stream-connector/dist';
const { SampleTimings } = require('./Configuration/sample-names');
const { loadSampleConfig } = require('./Configuration/sample-config');

async function main(): Promise<void> {
  const config = loadSampleConfig();

  const client1 = createClient(config.sessionEndpoint);
  const client2 = createClient(config.sessionEndpoint);
  try {
    await new BingoClientScenario().run(client1, client2);
  } finally {
    await Promise.allSettled([
      client1.close(),
      client2.close()
    ]);
  }

  console.log('PASS Bingo.Ts');
}

function createClient(sessionEndpoint: string): ZlinkStreamConnector {
  return connector.zlinkStreamConnectorFactory.create({
    endpoint: sessionEndpoint,
    codec: bingoProtobufCodec,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleTimings.requestTimeout,
    heartbeat: { enabled: false }
  });
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
