import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { spawn } from 'node:child_process';
import type { ChildProcess } from 'node:child_process';
import type { ClientOptions } from './client-options';

export interface DynamicProvider {
  readonly process: DynamicProcess;
  readonly httpUrl: string;
  readonly channelEndpoint: string;
}

export class DynamicClusterLauncher {
  private readonly processes: DynamicProcess[] = [];
  private constructor(
    private readonly registryMain: string,
    private readonly providerMain: string,
    private readonly logDir: string,
    readonly registryRouterEndpoint: string
  ) {}

  static async start(options: ClientOptions, scenarioName: string): Promise<DynamicClusterLauncher> {
    const registryRouterEndpoint = await pickEndpoint();
    const launcher = new DynamicClusterLauncher(options.registryMain, options.providerMain, options.logDir, registryRouterEndpoint);
    try {
      const registryHttp = await pickHttpUrl();
      const registry = launcher.startServer(
        `${scenarioName}-registry`,
        options.registryMain,
        [
          '--rid', `${scenarioName}-registry`,
          '--http-url', registryHttp,
          '--registry-pub-endpoint', await pickEndpoint(),
          '--registry-router-endpoint', registryRouterEndpoint,
          '--log-dir', options.logDir
        ],
        registryHttp,
        undefined
      );
      await registry.waitReady();
      return launcher;
    } catch (error) {
      await launcher.close();
      throw error;
    }
  }

  async startProvider(name: string, rid: string, weight = 100): Promise<DynamicProvider> {
    const httpUrl = await pickHttpUrl();
    const channelEndpoint = await pickEndpoint();
    let process: DynamicProcess | undefined;
    try {
      process = this.startServer(
        name,
        this.providerMain,
        [
          '--rid', rid,
          '--http-url', httpUrl,
          '--registry-router-endpoint', this.registryRouterEndpoint,
          '--channel-endpoint', channelEndpoint,
          '--route-endpoint', await pickEndpoint(),
          '--weight', weight.toString(),
          '--evidence-file', path.join(this.logDir, `${name}.evidence.log`),
          '--log-dir', this.logDir
        ],
        httpUrl,
        channelEndpoint
      );
      await process.waitReady();
      return { process, httpUrl, channelEndpoint };
    } catch (error) {
      if (process !== undefined) {
        await process.stop();
        const index = this.processes.indexOf(process);
        if (index >= 0) {
          this.processes.splice(index, 1);
        }
      }
      throw error;
    }
  }

  async stop(provider: DynamicProvider): Promise<void> {
    await provider.process.stop();
    const index = this.processes.indexOf(provider.process);
    if (index >= 0) {
      this.processes.splice(index, 1);
    }
  }

  async close(): Promise<void> {
    for (let i = this.processes.length - 1; i >= 0; i -= 1) {
      await this.processes[i].stop();
    }
    this.processes.length = 0;
  }

  private startServer(name: string, mainPath: string, args: readonly string[], httpUrl: string, channelEndpoint?: string): DynamicProcess {
    const child = spawn(process.execPath, [mainPath, ...args], { stdio: ['ignore', 'pipe', 'pipe'] });
    fs.mkdirSync(this.logDir, { recursive: true });
    child.stdout?.pipe(fs.createWriteStream(path.join(this.logDir, `${name}.stdout.log`)));
    child.stderr?.pipe(fs.createWriteStream(path.join(this.logDir, `${name}.stderr.log`)));
    const dynamicProcess = new DynamicProcess(child, httpUrl, channelEndpoint);
    this.processes.push(dynamicProcess);
    return dynamicProcess;
  }
}

export class DynamicProcess {
  constructor(
    private readonly process: ChildProcess,
    readonly httpUrl: string,
    readonly channelEndpoint?: string
  ) {}

  async waitReady(): Promise<void> {
    for (let i = 0; i < 120; i += 1) {
      if (this.process.exitCode !== null) {
        throw new Error(`Process exited before readiness: ${this.process.exitCode}`);
      }
      try {
        const response = await fetch(`${this.httpUrl}/health`);
        if (response.ok) {
          return;
        }
      } catch {
      }
      await new Promise((resolve) => setTimeout(resolve, 250));
    }
    throw new Error(`Process did not become ready: ${this.httpUrl}`);
  }

  async stop(): Promise<void> {
    if (this.process.exitCode !== null) {
      return;
    }
    const exited = new Promise<void>((resolve) => {
      this.process.once('exit', () => resolve());
    });
    try {
      await fetch(`${this.httpUrl}/shutdown`, { method: 'POST' });
    } catch {
      this.process.kill('SIGTERM');
    }
    if (this.process.exitCode !== null) {
      return;
    }
    const killer = setTimeout(() => {
      if (this.process.exitCode === null) {
        this.process.kill('SIGKILL');
      }
    }, 5000);
    await exited.finally(() => clearTimeout(killer));
  }
}

async function pickEndpoint(): Promise<string> {
  return `tcp://127.0.0.1:${await pickPort()}`;
}

async function pickHttpUrl(): Promise<string> {
  return `http://127.0.0.1:${await pickPort()}`;
}

function pickPort(): Promise<number> {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const address = server.address();
      const port = typeof address === 'object' && address !== null ? address.port : 0;
      server.close(() => resolve(port));
    });
  });
}
