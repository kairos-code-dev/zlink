import * as zlink from '@zlink-systems/zlink';
import { loadSampleConfig } from '../Configuration/sample-config';
import { SampleNames } from '../Configuration/sample-names';

type TopologyEntry = {
  channelName?: string;
  endpoint?: string;
  state?: number;
};

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const registryEndpoint = readOption('--registry-endpoint') ?? config.registryRouterEndpoint;
  const timeoutMs = Number(readOption('--timeout-ms') ?? '10000');
  await waitForTopology(registryEndpoint, timeoutMs, new Map([
    [SampleNames.apiChannel, config.apiEndpoint],
    [SampleNames.supportChannel, config.supportEndpoint],
    [SampleNames.notificationChannel, config.notificationEndpoint]
  ]));
  console.log('topology=ready');
}

async function waitForTopology(
  registryEndpoint: string,
  timeoutMs: number,
  requiredEndpoints: ReadonlyMap<string, string>
): Promise<void> {
  const context = zlink.createContext();
  const client = zlink.createRegistryQueryClient(context);
  const deadline = Date.now() + timeoutMs;
  let lastTopology: readonly TopologyEntry[] = [];
  try {
    client.connect(registryEndpoint);
    while (Date.now() < deadline) {
      lastTopology = client.topology() as readonly TopologyEntry[];
      if ([...requiredEndpoints].every(([channelName, endpoint]) =>
        lastTopology.some((entry) =>
          entry.channelName === channelName &&
          entry.endpoint === endpoint &&
          entry.state === 3))) {
        return;
      }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
  } finally {
    client.close();
    context.close();
  }

  console.error(JSON.stringify(lastTopology, (_key, value) => typeof value === 'bigint' ? value.toString() : value, 2));
  throw new Error('Timed out waiting for SupportChat sample topology readiness.');
}

function readOption(name: string): string | undefined {
  const index = process.argv.indexOf(name);
  return index >= 0 && index + 1 < process.argv.length ? process.argv[index + 1] : undefined;
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
