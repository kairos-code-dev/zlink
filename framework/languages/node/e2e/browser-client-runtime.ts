type BrowserE2eStatus = 'running' | 'passed' | 'failed';

interface BrowserE2eResult {
  readonly name: string;
  readonly status: BrowserE2eStatus;
  readonly error?: string;
}

declare global {
  interface Window {
    __zlinkE2eArgs?: readonly string[];
    __zlinkE2eResult?: BrowserE2eResult;
  }
}

function browserE2eArgs(): readonly string[] {
  return window.__zlinkE2eArgs ?? [];
}

async function runBrowserE2e(name: string, scenario: () => Promise<void>): Promise<void> {
  window.__zlinkE2eResult = { name, status: 'running' };
  try {
    await scenario();
    window.__zlinkE2eResult = { name, status: 'passed' };
  } catch (error) {
    const message = error instanceof Error ? `${error.name}: ${error.message}` : String(error);
    window.__zlinkE2eResult = { name, status: 'failed', error: message };
    console.error(error);
  }
}

async function browserE2eFetch(input: string, init?: RequestInit): Promise<Response> {
  const target = new URL(input, window.location.origin);
  if (target.origin === window.location.origin) {
    return await fetch(target, init);
  }
  return await fetch(`/proxy?url=${encodeURIComponent(target.toString())}`, init);
}

interface BrowserE2eHttpClient {
  get(path: string): BrowserE2eHttpRequest;
  post(path: string): BrowserE2eHttpRequest;
  close(): Promise<void>;
}

interface BrowserE2eHttpRequest {
  body(value: unknown): BrowserE2eHttpRequest;
  timeout(timeoutMs: number): BrowserE2eHttpRequest;
  fetch<T>(): Promise<T>;
}

class BrowserE2eHttpClientFactory {
  static create(baseUrl: string): BrowserE2eHttpClientBuilder {
    return new BrowserE2eHttpClientBuilder(baseUrl);
  }
}

class BrowserE2eHttpClientBuilder {
  private timeoutMs = 30_000;

  constructor(private readonly baseUrl: string) {}

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  build(): BrowserE2eHttpClient {
    return new DefaultBrowserE2eHttpClient(this.baseUrl, this.timeoutMs);
  }
}

class DefaultBrowserE2eHttpClient implements BrowserE2eHttpClient {
  constructor(private readonly baseUrl: string, private readonly timeoutMs: number) {}

  post(path: string): BrowserE2eHttpRequest {
    return new DefaultBrowserE2eHttpRequest('POST', new URL(path, `${this.baseUrl}/`).toString(), this.timeoutMs);
  }

  get(path: string): BrowserE2eHttpRequest {
    return new DefaultBrowserE2eHttpRequest('GET', new URL(path, `${this.baseUrl}/`).toString(), this.timeoutMs);
  }

  async close(): Promise<void> {}
}

class DefaultBrowserE2eHttpRequest implements BrowserE2eHttpRequest {
  private requestBody: unknown;

  constructor(private readonly method: 'GET' | 'POST', private readonly url: string, private timeoutMs: number) {}

  body(value: unknown): this {
    this.requestBody = value;
    return this;
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  async fetch<T>(): Promise<T> {
    const response = await browserE2eFetch(this.url, {
      method: this.method,
      headers: { 'content-type': 'application/json' },
      body: this.method === 'GET' || this.requestBody === undefined ? undefined : JSON.stringify(this.requestBody),
      signal: AbortSignal.timeout(this.timeoutMs)
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status} from ${this.url}: ${await response.text()}`);
    }
    const text = await response.text();
    return (text.length === 0 ? undefined : JSON.parse(text)) as T;
  }
}

export {
  BrowserE2eHttpClientFactory,
  browserE2eArgs,
  browserE2eFetch,
  runBrowserE2e
};

export type { BrowserE2eHttpClient, BrowserE2eResult };
