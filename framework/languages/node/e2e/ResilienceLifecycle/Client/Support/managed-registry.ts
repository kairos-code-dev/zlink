import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import type { ChildProcess } from 'node:child_process';

export interface RegistryStartOptions {
  readonly registryMain: string;
  readonly logDir: string;
  readonly name: string;
  readonly rid: string;
  readonly httpUrl: string;
  readonly registryPubEndpoint: string;
  readonly registryRouterEndpoint: string;
}

export function startRegistry(options: RegistryStartOptions): ManagedRegistryProcess {
  fs.mkdirSync(options.logDir, { recursive: true });
  const child = spawn(process.execPath, [
    options.registryMain,
    '--rid', options.rid,
    '--http-url', options.httpUrl,
    '--registry-pub-endpoint', options.registryPubEndpoint,
    '--registry-router-endpoint', options.registryRouterEndpoint,
    '--log-dir', options.logDir
  ], { stdio: ['ignore', 'pipe', 'pipe'] });
  child.stdout?.pipe(fs.createWriteStream(path.join(options.logDir, `${options.name}.stdout.log`)));
  child.stderr?.pipe(fs.createWriteStream(path.join(options.logDir, `${options.name}.stderr.log`)));
  return new ManagedRegistryProcess(child, options.httpUrl);
}

export class ManagedRegistryProcess {
  constructor(private readonly child: ChildProcess, private readonly healthUrl: string) {}

  async waitReady(): Promise<void> {
    const deadline = Date.now() + 30000;
    while (Date.now() < deadline) {
      if (this.child.exitCode !== null) {
        throw new Error(`Registry exited before readiness: ${this.child.exitCode}`);
      }
      try {
        const response = await fetch(`${this.healthUrl}/health`);
        if (response.ok) {
          return;
        }
      } catch {
      }
      await delay(250);
    }
    throw new Error(`Registry did not become ready: ${this.healthUrl}`);
  }

  async stop(): Promise<void> {
    if (this.child.exitCode !== null) {
      return;
    }
    const exited = new Promise<void>((resolve) => this.child.once('exit', () => resolve()));
    try {
      await fetch(`${this.healthUrl}/shutdown`, { method: 'POST' });
    } catch {
      this.child.kill('SIGTERM');
    }
    const killer = setTimeout(() => {
      if (this.child.exitCode === null) {
        this.child.kill('SIGKILL');
      }
    }, 5000);
    await exited.finally(() => clearTimeout(killer));
  }
}

function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}
