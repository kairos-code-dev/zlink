import type { ProfileReply, ProfileRequest } from '../../Shared/messages';
import { getJson, postJson } from './http-client';
import { ensure } from './scenario-assert';

interface TopologyEntryResult {
  readonly routingId?: string;
  readonly rid?: string;
  readonly endpoint?: string;
}

export function profileRequest(marker: string): ProfileRequest {
  return { value: 'fast', marker };
}

export async function sendRequestBatch(
  consumerUrl: string,
  markerPrefix: string,
  expectedProviderRid?: string
): Promise<boolean> {
  let sawExpectedProvider = expectedProviderRid === undefined;
  for (let i = 0; i < 32; i += 1) {
    const marker = `${markerPrefix}-${i}`;
    const reply = await postJson<ProfileReply>(consumerUrl, '/profile/request', profileRequest(marker));
    ensure(reply.value === 'profile:fast', `${markerPrefix} request returned an unexpected value.`);
    if (expectedProviderRid !== undefined && reply.providerRid === expectedProviderRid) {
      sawExpectedProvider = true;
    }
  }
  return sawExpectedProvider;
}

export async function waitTopologyReady(registryUrl: string, rid: string): Promise<void> {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    const entries = await getJson<TopologyEntryResult[]>(registryUrl, '/registry/topology');
    if (entries.some((entry) => entry.routingId === rid || entry.rid === rid)) {
      return;
    }
    await delay(250);
  }
  throw new Error(`Timed out waiting for topology rid=${rid} Ready.`);
}

export async function waitTopologyEndpointReady(registryUrl: string, rid: string, endpoint: string): Promise<void> {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    const entries = await getJson<TopologyEntryResult[]>(registryUrl, '/registry/topology');
    if (entries.some((entry) => (entry.routingId === rid || entry.rid === rid) && entry.endpoint === endpoint)) {
      return;
    }
    await delay(250);
  }
  throw new Error(`Timed out waiting for topology rid=${rid} endpoint=${endpoint} Ready.`);
}

export async function waitTopologyAbsent(registryUrl: string, rid: string): Promise<void> {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    const entries = await getJson<TopologyEntryResult[]>(registryUrl, '/registry/topology');
    if (!entries.some((entry) => entry.routingId === rid || entry.rid === rid)) {
      return;
    }
    await delay(250);
  }
  throw new Error(`Timed out waiting for topology rid=${rid} to leave Ready.`);
}

export async function waitTopologyEndpointAbsent(registryUrl: string, endpoint: string): Promise<void> {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    const entries = await getJson<TopologyEntryResult[]>(registryUrl, '/registry/topology');
    if (!entries.some((entry) => entry.endpoint === endpoint)) {
      return;
    }
    await delay(250);
  }
  throw new Error(`Timed out waiting for topology endpoint=${endpoint} to leave Ready.`);
}

export async function waitUntilAvailable(baseUrl: string): Promise<void> {
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`${baseUrl}/health`);
      if (response.ok) {
        return;
      }
    } catch {
    }
    await delay(100);
  }
  throw new Error(`Timed out waiting for provider health: ${baseUrl}`);
}

export async function waitUntilDown(baseUrl: string): Promise<void> {
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`${baseUrl}/health`);
      if (!response.ok) {
        return;
      }
    } catch {
      return;
    }
    await delay(100);
  }
  throw new Error(`Timed out waiting for provider shutdown: ${baseUrl}`);
}

function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}
