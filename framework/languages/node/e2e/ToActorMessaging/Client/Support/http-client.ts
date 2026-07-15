import { browserE2eFetch } from '../../../browser-client-runtime';

export async function getJson<T>(url: string): Promise<T> {
  const response = await browserE2eFetch(url);
  if (!response.ok) throw new Error(`${url} failed with ${response.status}`);
  return await response.json() as T;
}

export async function postJson<T>(url: string, body: unknown): Promise<T> {
  const response = await browserE2eFetch(url, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body)
  });
  if (!response.ok) throw new Error(`${url} failed with ${response.status}`);
  return await response.json() as T;
}
