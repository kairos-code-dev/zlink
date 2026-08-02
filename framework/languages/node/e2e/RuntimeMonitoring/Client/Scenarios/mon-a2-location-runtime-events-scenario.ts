// MON-A2: Peer가 추가되고 제거된 결과를 관찰한다 시나리오를 검증한다.
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../../../http-client';
import { type ManagedProcess, startServiceB, waitForPortState } from '../Support/managed-service';
import { ensure } from '../Support/scenario-assert';

interface Projection {
  readonly topology: number;
  readonly summary: number;
  readonly topologyEvents: number;
  readonly summaryEvents: number;
}

export async function runMonA2(options: ClientOptions): Promise<ManagedProcess> {
  const baseline = await waitForProjection(options, undefined, (value) => value.topology > 0 && value.summary > 0);

  await postJson<object>(options.serviceBUrl, '/shutdown', {});
  await waitForPortState(options.serviceBUrl, false, 'MON-A2 expected svc-b to stop.');
  const removed = await waitForProjection(options, baseline, (value) =>
    value.topology < baseline.topology && value.summary < baseline.summary);
  ensure(removed.topology < baseline.topology, 'MON-A2 topology payload did not reflect provider removal.');
  ensure(removed.summary < baseline.summary, 'MON-A2 service summary payload did not reflect provider removal.');

  const restarted = startServiceB(options, 'svc-b-mon-a2');
  try {
    await waitForPortState(options.serviceBUrl, true, 'MON-A2 expected svc-b to restart.');
    const restored = await waitForProjection(options, removed, (value) =>
      value.topology >= baseline.topology && value.summary >= baseline.summary);
    ensure(restored.topology >= baseline.topology, 'MON-A2 topology payload did not reflect provider addition.');
    ensure(restored.summary >= baseline.summary, 'MON-A2 service summary payload did not reflect provider addition.');
    console.log('scenario MON-A2 passed');
    return restarted;
  } catch (error) {
    await restarted.stop();
    throw error;
  }
}

async function waitForProjection(
  options: ClientOptions,
  after: Projection | undefined,
  accept: (value: Projection) => boolean
): Promise<Projection> {
  const deadline = Date.now() + 20_000;
  while (Date.now() < deadline) {
    const value = projection(await getJson<string[]>(options.serviceUrl, '/evidence'));
    const hasNewEvents = after === undefined
      || (value.topologyEvents > after.topologyEvents && value.summaryEvents > after.summaryEvents);
    if (hasNewEvents && accept(value)) return value;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error('MON-A2 timed out waiting for a location projection change.');
}

function projection(lines: readonly string[]): Projection {
  const topology = values(lines, 'TopologyChanged', 'topology');
  const summary = values(lines, 'ServiceSummaryChanged', 'summaryTotal');
  return {
    topology: topology.at(-1) ?? -1,
    summary: summary.at(-1) ?? -1,
    topologyEvents: topology.length,
    summaryEvents: summary.length
  };
}

function values(lines: readonly string[], kind: string, key: string): number[] {
  return lines
    .filter((line) => line.includes(`monitor-location|source=monitor.location-runtime|kind=${kind}`))
    .map((line) => Number.parseInt(line.match(new RegExp(`(?:^|\\|)${key}=(-?\\d+)`))?.[1] ?? '-1', 10));
}
