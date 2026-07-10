import type {
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkSpot,
  ZLinkSpotCreateResult,
  ZLinkSpotInfo
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSpotCreateState
} from '../../contracts';
import type { ZLinkBackendSpot } from '../backend/contracts';
import type { ZLinkSpotActorHandlerRegistryRuntime } from '../actors';
import type { DefaultZLinkSpotHandlerRegistry } from './spot-handler-registry';
import type { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import type { ZLinkSpotTimerRegistry } from './spot-timer';

export interface ZLinkSpotActivation {
  readonly spotRid: RoutingId;
  readonly spotType: Type<ZLinkSpot>;
  readonly spot: ZLinkSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly timers: ZLinkSpotTimerRegistry;
  readonly actors: Map<string, ZLinkActor>;
  readonly leftActors: Set<string>;
  readonly actorHandlers: ZLinkSpotActorHandlerRegistryRuntime;
  readonly handlers: DefaultZLinkSpotHandlerRegistry;
  readonly actorCount: () => number;
  readonly nativeSpot?: ZLinkBackendSpot;
}

interface PendingSpotActivation {
  readonly spotType: Type<ZLinkSpot>;
  readonly ready: Promise<ZLinkSpotCreateResult>;
}

export class ZLinkSpotActivationRegistry {
  private nextId = 1;
  private readonly activations = new Map<string, ZLinkSpotActivation>();
  private readonly pending = new Map<string, PendingSpotActivation>();

  allocateSpotRid(): RoutingId {
    let spotRid: RoutingId;
    do {
      spotRid = `spot-${this.nextId}`;
      this.nextId += 1;
    } while (this.activations.has(spotActivationKey(spotRid)));
    return spotRid;
  }

  resolve(spotRid: RoutingId): ZLinkSpotActivation | undefined {
    return this.activations.get(spotActivationKey(spotRid));
  }

  has(spotRid: RoutingId): boolean {
    return this.activations.has(spotActivationKey(spotRid));
  }

  list(): readonly ZLinkSpotInfo[] {
    return [...this.activations.values()]
      .map((activation) => String(activation.spotRid))
      .sort((left, right) => left.localeCompare(right))
      .map((spotRid) => ({ spotRid }));
  }

  register(activation: ZLinkSpotActivation): void {
    this.activations.set(spotActivationKey(activation.spotRid), activation);
  }

  takeClosable(spotRid: RoutingId): ZLinkSpotActivation | undefined {
    const key = spotActivationKey(spotRid);
    const activation = this.activations.get(key);
    if (activation === undefined || activation.actorCount() > 0) {
      return undefined;
    }
    this.activations.delete(key);
    return activation;
  }

  async existingOrPendingResult(
    spotType: Type<ZLinkSpot>,
    spotRid: RoutingId
  ): Promise<ZLinkSpotCreateResult | undefined> {
    const key = spotActivationKey(spotRid);
    const existing = this.activations.get(key);
    if (existing !== undefined) {
      if (existing.spotType !== spotType) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.SpotTypeMismatch,
          `Spot '${spotRid}' already exists with a different spot type.`
        );
      }
      return { spotRid, state: ZLinkSpotCreateState.Existing };
    }

    const pending = this.pending.get(key);
    if (pending === undefined) {
      return undefined;
    }
    if (pending.spotType !== spotType) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.SpotTypeMismatch,
        `Spot '${spotRid}' is being created with a different spot type.`
      );
    }
    const result = await pending.ready;
    return result.state === ZLinkSpotCreateState.Created
      ? { spotRid, state: ZLinkSpotCreateState.Existing }
      : { spotRid, state: result.state, reply: result.reply };
  }

  async trackPending(
    spotType: Type<ZLinkSpot>,
    spotRid: RoutingId,
    ready: Promise<ZLinkSpotCreateResult>
  ): Promise<ZLinkSpotCreateResult> {
    const key = spotActivationKey(spotRid);
    this.pending.set(key, { spotType, ready });
    try {
      return await ready;
    } finally {
      this.pending.delete(key);
    }
  }
}

function spotActivationKey(spotRid: RoutingId): string {
  return String(spotRid);
}
