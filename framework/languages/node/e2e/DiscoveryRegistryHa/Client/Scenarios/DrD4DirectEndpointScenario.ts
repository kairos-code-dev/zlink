import type { ClientOptions } from '../Support/client-options';
import { getJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import {
  normalizeTopology,
  type TopologySnapshotEntry,
  waitForReadyTopologyProviderSet
} from '../Support/discovery-scenario-support';

export async function runDrD4(options: ClientOptions): Promise<void> {
  ensure(options.probeUrl !== undefined, 'DR-D4 requires probe-url.');

  await waitForReadyTopologyProviderSet(options.registryUrl, ['api-a']);
  await waitForReadyTopologyProviderSet(options.probeUrl, ['api-a']);

  const local = normalizeTopology(await getJson<readonly TopologySnapshotEntry[]>(options.registryUrl, '/registry/topology'));
  const remote = normalizeTopology(await getJson<readonly TopologySnapshotEntry[]>(options.probeUrl, '/registry/topology'));
  ensure(local.length > 0, 'DR-D4 topology snapshot was empty.');
  ensure(
    local.length === remote.length && local.every((entry, index) => entry === remote[index]),
    'DR-D4 in-process and remote topology snapshots did not match.'
  );
  console.log('scenario DR-D4 passed');
}
