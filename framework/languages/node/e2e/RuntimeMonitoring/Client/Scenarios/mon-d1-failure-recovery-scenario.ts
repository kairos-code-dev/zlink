// MON-D1: 장애·복구 반복 중 이벤트 연속성 시나리오를 검증한다.
import fs from 'node:fs';
import type { EvidenceWaitReq, ProfileRes, ProfileReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../../../http-client';
import { type ManagedProcess, startServiceB, waitForPortState } from '../Support/managed-service';
import { ensure } from '../Support/scenario-assert';

export async function runMonD1(options: ClientOptions): Promise<ManagedProcess> {
  const baseline = await topologyCount(options.serviceUrl);
  await postJson<object>(options.serviceBUrl, '/shutdown', {});
  await waitForPortState(options.serviceBUrl, false, 'MON-D1 expected service-b to stop.');

  const restarted = startServiceB(options, 'svc-b-restart');
  try {
    await waitForPortState(options.serviceBUrl, true, 'MON-D1 expected service-b to restart.');

    const request: ProfileReq = { value: 'restart', marker: 'mon-d1-request' };
    const reply = await postJson<ProfileRes>(options.triggerUrl, '/profile/request/service-b', request);
    ensure(
      reply.providerRid === 'svc-b'
      && reply.marker === 'mon-d1-request'
      && reply.value === 'profile:restart',
      'MON-D1 restarted service did not handle request.'
    );

    const evidence = await postJson<string[]>(options.serviceBUrl, '/evidence/wait', {
      containsAll: ['profile-request|rid=svc-b|marker=mon-d1-request|value=restart'],
      containsAnyGroups: [],
      timeoutMilliseconds: 15000
    } satisfies EvidenceWaitReq);
    ensure(
      evidence.some((line) => line.includes('profile-request|rid=svc-b|marker=mon-d1-request|value=restart')),
      'MON-D1 restarted service evidence missing.'
    );

    const locationEvidence = await waitForTopologyContinuity(options, baseline);
    ensure(
      locationEvidence.length > 0,
      'MON-D1 location topology continuity evidence missing.'
    );
    console.log('scenario MON-D1 passed');
    return restarted;
  } catch (error) {
    await restarted.stop();
    throw error;
  }
}

async function waitForTopologyContinuity(options: ClientOptions, baseline: number): Promise<string[]> {
  const deadline = Date.now() + 15000;
  while (Date.now() <= deadline) {
    const lines = [
      ...await getJson<string[]>(options.serviceUrl, '/evidence'),
      ...readEvidenceFile(`${options.logDir}/svc-b.evidence.log`)
    ];
    const topology = lines.filter((line) =>
      line.includes('monitor-location|source=monitor.location-runtime|kind=TopologyChanged'));
    if (topology.length > baseline) {
      return topology.slice(baseline);
    }
    const restartTopology = topology.filter((line) => line.includes('topology=') && !line.includes('topology=0'));
    if (restartTopology.length > 0 && readEvidenceFile(`${options.logDir}/svc-b.evidence.log`).some((line) =>
      line.includes('profile-request|rid=svc-b|marker=mon-d1-request'))) {
      return restartTopology;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error('Timed out waiting for location topology continuity evidence.');
}

async function topologyCount(serviceUrl: string): Promise<number> {
  const lines = await getJson<string[]>(serviceUrl, '/evidence');
  return lines.filter((line) => line.includes('monitor-location|source=monitor.location-runtime|kind=TopologyChanged')).length;
}

function readEvidenceFile(path: string): string[] {
  try {
    return fs.readFileSync(path, 'utf8').split(/\r?\n/).filter((line) => line.length > 0);
  } catch {
    return [];
  }
}
