import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import type { ChildProcess } from 'node:child_process';
import type { ClientOptions } from './client-options';

export class ServerProcessLauncher {
  constructor(private readonly options: ClientOptions) {}

  startSubscriber(name: string, httpUrl: string, evidenceFile: string): DynamicProcess {
    return this.start(
      name,
      this.options.subscriberMain,
      [
        '--rid', name,
        '--http-url', httpUrl,
        '--registry-router-endpoint', this.options.registryRouterEndpoint,
        '--publisher-endpoint', this.options.publisherEndpoint,
        '--evidence-file', path.join(this.options.logDir, evidenceFile),
        '--log-dir', this.options.logDir
      ],
      httpUrl
    );
  }

  startPublisher(): DynamicProcess {
    return this.start(
      'pub-restart',
      this.options.publisherMain,
      [
        '--rid', 'pub-a',
        '--http-url', this.options.publisherUrl,
        '--registry-router-endpoint', this.options.registryRouterEndpoint,
        '--publisher-endpoint', this.options.publisherEndpoint,
        '--evidence-file', path.join(this.options.logDir, 'pub-restart.evidence.log'),
        '--log-dir', this.options.logDir
      ],
      this.options.publisherUrl
    );
  }

  private start(name: string, mainPath: string, args: readonly string[], httpUrl: string): DynamicProcess {
    fs.mkdirSync(this.options.logDir, { recursive: true });
    const child = spawn(process.execPath, [mainPath, ...args], {
      stdio: ['ignore', 'pipe', 'pipe'],
      env: { ...process.env, ZLINK_E2E_RID: args[1] ?? name }
    });
    child.stdout?.pipe(fs.createWriteStream(path.join(this.options.logDir, `${name}.stdout.log`)));
    child.stderr?.pipe(fs.createWriteStream(path.join(this.options.logDir, `${name}.stderr.log`)));
    return new DynamicProcess(child, httpUrl);
  }
}

export class DynamicProcess {
  constructor(
    private readonly child: ChildProcess,
    readonly httpUrl: string
  ) {}

  get hasExited(): boolean {
    return this.child.exitCode !== null || this.child.signalCode !== null;
  }

  async waitReady(): Promise<void> {
    for (let i = 0; i < 120; i += 1) {
      if (this.hasExited) {
        throw new Error(`Process exited before readiness: ${this.child.exitCode ?? this.child.signalCode}`);
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
    if (this.hasExited) {
      return;
    }
    const exited = new Promise<void>((resolve) => {
      this.child.once('exit', () => resolve());
    });
    try {
      await fetch(`${this.httpUrl}/shutdown`, { method: 'POST' });
    } catch {
      this.child.kill('SIGTERM');
    }
    if (this.hasExited) {
      return;
    }
    const killer = setTimeout(() => {
      if (!this.hasExited) {
        this.child.kill('SIGKILL');
      }
    }, 5000);
    await exited.finally(() => clearTimeout(killer));
  }

  async kill(): Promise<void> {
    if (this.hasExited) {
      return;
    }
    const exited = new Promise<void>((resolve) => {
      this.child.once('exit', () => resolve());
    });
    this.child.kill('SIGKILL');
    await exited;
  }
}
