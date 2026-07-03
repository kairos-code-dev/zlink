export interface LocationProbeOptions {
  readonly rid: string;
  readonly probeId: number;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly peers: readonly string[];
}

export function parseLocationProbeOptions(args: readonly string[]): LocationProbeOptions {
  const values = parseArgs(args);
  return {
    rid: values.get('rid') ?? 'reg-1',
    probeId: parseInteger(values.get('probe-id') ?? '1', 'probe-id'),
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    logDir: values.get('log-dir') ?? 'logs',
    redisEndpoint: required(values, 'redis-endpoint'),
    redisKeyPrefix: required(values, 'redis-key-prefix'),
    peers: values.get('peer') === undefined ? [] : values.getAll('peer')
  };
}

export function parseArgs(args: readonly string[]): MultiMap {
  const values = new MultiMap();
  for (let i = 0; i < args.length; i += 1) {
    const key = args[i];
    if (!key.startsWith('--') || i + 1 >= args.length) {
      throw new Error(`Invalid argument '${key}'.`);
    }
    values.add(key.slice(2), args[++i]);
  }
  return values;
}

export function required(values: MultiMap, name: string): string {
  const value = values.get(name);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${name} is required.`);
  }
  return value;
}

class MultiMap {
  private readonly values = new Map<string, string[]>();

  add(key: string, value: string): void {
    const current = this.values.get(key) ?? [];
    current.push(value);
    this.values.set(key, current);
  }

  get(key: string): string | undefined {
    return this.values.get(key)?.[0];
  }

  getAll(key: string): readonly string[] {
    return this.values.get(key) ?? [];
  }
}

function parseInteger(value: string, name: string): number {
  const parsed = Number.parseInt(value, 10);
  if (!Number.isInteger(parsed)) {
    throw new Error(`--${name} must be an integer.`);
  }
  return parsed;
}
