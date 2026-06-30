import fs from 'node:fs';
import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { postJson } from './http-client';

export class ManagedProcess {
  private constructor(
    private readonly process: ChildProcessWithoutNullStreams,
    private readonly httpUrl: string
  ) {}

  static start(options: {
    readonly name: string;
    readonly rid: string;
    readonly main: string;
    readonly args: readonly string[];
    readonly logDir: string;
  }): ManagedProcess {
    fs.mkdirSync(options.logDir, { recursive: true });
    const child = spawn(process.execPath, [options.main, ...options.args], {
      env: { ...process.env, ZLINK_E2E_RID: options.rid },
      stdio: ['ignore', 'pipe', 'pipe']
    });
    child.stdout.pipe(fs.createWriteStream(`${options.logDir}/${options.name}.stdout.log`));
    child.stderr.pipe(fs.createWriteStream(`${options.logDir}/${options.name}.stderr.log`));
    return new ManagedProcess(child, argumentValue(options.args, '--http-url'));
  }

  async waitReady(): Promise<void> {
    const deadline = Date.now() + 30000;
    while (Date.now() < deadline) {
      if (this.process.exitCode !== null) {
        throw new Error(`Process exited before readiness: ${this.process.exitCode}.`);
      }
      try {
        const response = await fetch(new URL('/health', this.httpUrl));
        if (response.ok) {
          return;
        }
      } catch {
      }
      await new Promise((resolve) => setTimeout(resolve, 250));
    }
    throw new Error(`Timed out waiting for ${this.httpUrl}.`);
  }

  async stop(): Promise<void> {
    if (this.process.exitCode !== null) {
      return;
    }
    try {
      await postJson<unknown>(this.httpUrl, '/shutdown', {});
    } catch {
    }
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      if (this.process.exitCode !== null) {
        return;
      }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    this.process.kill('SIGKILL');
  }
}

function argumentValue(args: readonly string[], key: string): string {
  const index = args.indexOf(key);
  if (index < 0 || index + 1 >= args.length) {
    throw new Error(`${key} is required.`);
  }
  return args[index + 1];
}
