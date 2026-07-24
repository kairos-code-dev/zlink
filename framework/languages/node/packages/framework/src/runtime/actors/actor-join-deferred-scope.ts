import { AsyncLocalStorage } from 'node:async_hooks';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';

const MAX_DEFERRED_JOINS = 64;
const MAX_DEFERRED_JOIN_BYTES = 8 * 1024 * 1024;

interface DeferredActorJoin {
  readonly requestBytes: number;
  readonly execute: () => Promise<void>;
  readonly discard: () => void;
}

class ActorJoinRegistrationScope {
  private readonly intents: DeferredActorJoin[] = [];
  private totalBytes = 0;
  private open = true;

  register(intent: DeferredActorJoin): void {
    if (!this.open) {
      intent.discard();
      throw new ZLinkConfigurationException(
        'Actor join defer requires an open Framework actor handler scope.'
      );
    }
    if (this.intents.length >= MAX_DEFERRED_JOINS) {
      intent.discard();
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.InvalidConfiguration,
        `An actor handler may defer at most ${MAX_DEFERRED_JOINS} joins.`
      );
    }
    if (this.totalBytes + intent.requestBytes > MAX_DEFERRED_JOIN_BYTES) {
      intent.discard();
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.InvalidConfiguration,
        `Deferred actor join requests may use at most ${MAX_DEFERRED_JOIN_BYTES} bytes per handler.`
      );
    }
    this.intents.push(intent);
    this.totalBytes += intent.requestBytes;
  }

  async activate(): Promise<void> {
    this.open = false;
    for (const intent of this.intents) {
      await intent.execute();
    }
  }

  discard(): void {
    this.open = false;
    for (const intent of this.intents) intent.discard();
  }
}

const actorJoinScope = new AsyncLocalStorage<ActorJoinRegistrationScope>();

export function deferActorJoin(intent: DeferredActorJoin): void {
  const scope = actorJoinScope.getStore();
  if (scope === undefined) {
    intent.discard();
    throw new ZLinkConfigurationException(
      'Actor join defer requires an open Framework actor handler scope.'
    );
  }
  scope.register(intent);
}

export async function runActorHandlerWithDeferredJoins<T>(
  handler: () => Promise<T> | T
): Promise<T> {
  const scope = new ActorJoinRegistrationScope();
  try {
    const result = await actorJoinScope.run(scope, async () => handler());
    await scope.activate();
    return result;
  } catch (error) {
    scope.discard();
    throw error;
  }
}
