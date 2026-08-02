export async function getJson<T>(baseUrl: string, path: string): Promise<T> {
  return await request<T>(baseUrl, path, 'GET');
}

export async function postJson<T>(baseUrl: string, path: string, body?: unknown): Promise<T> {
  return await request<T>(baseUrl, path, 'POST', body);
}

async function request<T>(baseUrl: string, path: string, method: 'GET' | 'POST', body?: unknown): Promise<T> {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 10_000);
  try {
    const response = await fetch(new URL(path, baseUrl), {
      method,
      headers: body === undefined ? undefined : { 'content-type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: controller.signal
    });
    const value = await response.json() as T;
    if (!response.ok) throw new Error(`HTTP ${response.status}: ${JSON.stringify(value)}`);
    return value;
  } finally {
    clearTimeout(timeout);
  }
}
