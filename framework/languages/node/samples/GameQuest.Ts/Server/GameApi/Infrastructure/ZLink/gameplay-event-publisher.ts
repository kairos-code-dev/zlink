import { requestQuest } from '../../gamequest-session';
import type { ZLinkRouteClient } from '@zlink-systems/framework';

class GameplayEventPublisher {
  constructor(private readonly routes: ZLinkRouteClient) {}

  async publish<TResponse>(payload: unknown): Promise<TResponse> {
    return this.request<TResponse>(payload);
  }

  async request<TResponse>(payload: unknown): Promise<TResponse> {
    let lastError: unknown;
    for (let attempt = 0; attempt < 40; attempt += 1) {
      try {
        return await requestQuest<TResponse>(this.routes, payload);
      } catch (error) {
        lastError = error;
        await delay(250);
      }
    }
    throw lastError instanceof Error ? lastError : new Error(String(lastError));
  }
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

export {
  GameplayEventPublisher
};
