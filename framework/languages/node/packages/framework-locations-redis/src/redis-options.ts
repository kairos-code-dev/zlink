import type { RedisClientOptions } from 'redis';

export type RedisCommandValue = string | Buffer | number;

export interface RedisCommandClient {
  isOpen?: boolean;
  connect(): Promise<unknown>;
  sendCommand(args: RedisCommandValue[]): Promise<unknown>;
  quit?(): Promise<unknown>;
  disconnect?(): Promise<unknown>;
  on?(event: 'error', listener: (error: unknown) => void): unknown;
}

export interface ZLinkRedisLocationOptions {
  readonly url?: string;
  readonly client?: RedisCommandClient;
  readonly clientOptions?: RedisClientOptions;
  readonly keyPrefix: string;
}

export interface ZLinkRedisRelocationOptions {
  readonly url?: string;
  readonly client?: RedisCommandClient;
  readonly clientOptions?: RedisClientOptions;
  readonly keyPrefix: string;
}

export class MutableZLinkRedisLocationOptions {
  url?: string;
  client?: RedisCommandClient;
  clientOptions?: RedisClientOptions;
  keyPrefix = '';

  setUrl(url: string): this {
    this.url = url;
    return this;
  }

  setClient(client: RedisCommandClient): this {
    this.client = client;
    return this;
  }

  setClientOptions(options: RedisClientOptions): this {
    this.clientOptions = options;
    return this;
  }

  setKeyPrefix(keyPrefix: string): this {
    this.keyPrefix = keyPrefix;
    return this;
  }
}

export class MutableZLinkRedisRelocationOptions {
  url?: string;
  client?: RedisCommandClient;
  clientOptions?: RedisClientOptions;
  keyPrefix = '';

  setUrl(url: string): this { this.url = url; return this; }
  setClient(client: RedisCommandClient): this { this.client = client; return this; }
  setClientOptions(options: RedisClientOptions): this { this.clientOptions = options; return this; }
  setKeyPrefix(keyPrefix: string): this { this.keyPrefix = keyPrefix; return this; }
}

export function configureOptions(
  configure: (options: MutableZLinkRedisLocationOptions) => void
): ZLinkRedisLocationOptions {
  const options = new MutableZLinkRedisLocationOptions();
  configure(options);
  return options;
}
