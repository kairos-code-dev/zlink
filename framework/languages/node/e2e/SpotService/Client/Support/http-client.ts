import { browserE2eFetch } from '../../../browser-client-runtime';

export async function postJson<T>(baseUrl: string, path: string, body?: unknown): Promise<T> {
  const response = await browserE2eFetch(`${baseUrl}${path}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body)
  });
  if (!response.ok) {
    throw new Error(`POST ${path} failed: ${response.status} ${await response.text()}`);
  }
  return await response.json() as T;
}
