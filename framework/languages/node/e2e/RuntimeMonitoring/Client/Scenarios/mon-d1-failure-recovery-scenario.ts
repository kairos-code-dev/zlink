import { spawn } from 'node:child_process';
import type { ChildProcess } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import type { EvidenceWaitReq, ProfileRes, ProfileReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonD1(options: ClientOptions): Promise<void> {
  const baseline = await topologyCount(options.serviceUrl);
  await postJson<object>(options.serviceBUrl, '/shutdown', {});
  await waitForPortState(options.serviceBUrl, false, 'MON-D1 expected service-b to stop.');

  const restarted = startServiceB(options);
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
  } finally {
    await postBestEffort(options.serviceBUrl, '/shutdown');
    await restarted.stop();
  }

  console.log('scenario MON-D1 passed');
}

function startServiceB(options: ClientOptions): ManagedProcess {
  const stdout = fs.openSync(`${options.logDir}/svc-b-restart.stdout.log`, 'w');
  const stderr = fs.openSync(`${options.logDir}/svc-b-restart.stderr.log`, 'w');
  const child = spawn(process.execPath, [
    options.serviceMain,
    '--config', options.serviceBConfig
  ], {
    env: { ...process.env, ZLINK_E2E_RID: 'svc-b' },
    stdio: ['ignore', stdout, stderr]
  });
  return new ManagedProcess(child);
}

class ManagedProcess {
  constructor(private readonly child: ChildProcess) {}

  async stop(): Promise<void> {
    try {
      await this.wait(5000);
      return;
    } catch {
    }

    this.child.kill('SIGTERM');
    try {
      await this.wait(5000);
      return;
    } catch {
    }

    this.child.kill('SIGKILL');
    await this.wait(5000);
  }

  async wait(timeoutMs = 30000): Promise<void> {
    if (this.child.exitCode !== null || this.child.signalCode !== null) {
      return;
    }
    await Promise.race([
      new Promise<void>((resolve) => this.child.once('exit', () => resolve())),
      new Promise<void>((_resolve, reject) => setTimeout(() => reject(new Error('Service process did not exit.')), timeoutMs))
    ]);
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

async function postBestEffort(baseUrl: string, path: string): Promise<void> {
  try {
    await postJson<object>(baseUrl, path, {});
  } catch {
  }
}

async function waitForPortState(baseUrl: string, shouldBeOpen: boolean, failureMessage: string): Promise<void> {
  const url = new URL(baseUrl);
  for (let attempt = 0; attempt < 100; attempt += 1) {
    if (await canConnect(url.hostname, Number(url.port)) === shouldBeOpen) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(failureMessage);
}

async function canConnect(host: string, port: number): Promise<boolean> {
  return await new Promise<boolean>((resolve) => {
    const socket = net.createConnection({ host, port });
    socket.setTimeout(200);
    socket.once('connect', () => {
      socket.destroy();
      resolve(true);
    });
    socket.once('timeout', () => {
      socket.destroy();
      resolve(false);
    });
    socket.once('error', () => resolve(false));
  });
}
