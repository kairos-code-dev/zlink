import { ZLinkHttpClient } from '@zlink-systems/http-client';
import * as connector from '@zlink-systems/stream-connector';
import { loadSampleConfig } from './Configuration/sample-config';
import { DeliveryDispatchClientScenario } from './deliverydispatch-client-scenario';
import { SampleTimings } from '../Shared/Configuration/sample-names';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const http = ZLinkHttpClient.create(config.dispatchApiHttpUrl)
    .json()
    .timeout(SampleTimings.clientTimeout)
    .build();
  const customer = createClient(config.sessionStreamEndpoint);
  try {
    await new DeliveryDispatchClientScenario().run(
      http,
      customer,
      AbortSignal.timeout(SampleTimings.clientTimeout)
    );
  } finally {
    await Promise.allSettled([
      customer.close(),
      http.close()
    ]);
  }
  console.log('deliverydispatch=completed');
  console.log('PASS DeliveryDispatch.Ts');
}

function createClient(sessionEndpoint: string): ZlinkStreamConnector {
  return connector.zlinkStreamConnectorFactory.create({
    endpoint: sessionEndpoint,
    codec: connector.zlinkStreamJsonCodec,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleTimings.requestTimeout,
    waitTimeoutMs: SampleTimings.clientTimeout,
    heartbeat: { enabled: false }
  });
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
