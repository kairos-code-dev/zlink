import * as zlink from '@zlink-systems/zlink';
import { SampleNames } from '../../Shared/Configuration/sample-names';

async function waitForTopology(registryEndpoint: string, timeoutMs: number): Promise<void> {
  const context = zlink.createContext();
  const client = zlink.createRegistryQueryClient(context);
  const deadline = Date.now() + timeoutMs;
  try {
    client.connect(registryEndpoint);
    while (Date.now() < deadline) {
      const entries = client.topology();
      const ready = [
        SampleNames.dispatchChannel,
        SampleNames.courierRouteChannel,
        SampleNames.courierActorNodeRouteChannel,
        SampleNames.trackingChannel
      ].every((channel) => entries.some((entry) =>
        entry.channelName === channel &&
        entry.state === 3 &&
        typeof entry.endpoint === 'string' &&
        entry.endpoint.length > 0
      ));
      if (ready) {
        console.log('topology=ready');
        return;
      }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    throw new Error('Timed out waiting for DeliveryDispatch topology readiness.');
  } finally {
    client.close();
    context.close();
  }
}

export { waitForTopology };
