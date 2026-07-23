import type { Type } from '../Common';
import type { ZLinkActor } from '../Actors';
import type { ZLinkInstanceSpot, ZLinkSpot } from '../Spots';
import type {
  ZLinkAffinityKey,
  ZLinkPlacementProfile
} from '../Locations';

export interface ZLinkObjectPlacementOptions {
  readonly placementProfiles?: readonly ZLinkPlacementProfile[];
  readonly maxActiveObjects?: number;
  readonly maxPendingActivations?: number;
}

export interface ZLinkActorRelocationAdapter<TActor extends ZLinkActor> {
  capture(actor: TActor, signal: AbortSignal): Promise<Uint8Array>;
  restore(actor: TActor, payload: Uint8Array, signal: AbortSignal): Promise<void>;
}

export interface ZLinkSpotRelocationAdapter<
  TSpot extends ZLinkSpot | ZLinkInstanceSpot
> {
  capture(spot: TSpot, signal: AbortSignal): Promise<Uint8Array>;
  restore(spot: TSpot, payload: Uint8Array, signal: AbortSignal): Promise<void>;
}

declare const zlinkRelocationPolicyBrand: unique symbol;

export interface ZLinkDisabledRelocationPolicy<TInstance> {
  readonly [zlinkRelocationPolicyBrand]: TInstance;
  readonly kind: 'disabled';
}

export interface ZLinkRecreateRelocationPolicy<TInstance> {
  readonly [zlinkRelocationPolicyBrand]: TInstance;
  readonly kind: 'recreate';
}

export interface ZLinkSnapshotRelocationPolicy<TInstance> {
  readonly [zlinkRelocationPolicyBrand]: TInstance;
  readonly kind: 'snapshot';
  readonly adapterType: Type;
}

export type ZLinkRelocationPolicy<TInstance> =
  | ZLinkDisabledRelocationPolicy<TInstance>
  | ZLinkRecreateRelocationPolicy<TInstance>
  | ZLinkSnapshotRelocationPolicy<TInstance>;

export function zlinkDisabledRelocation<T>(): ZLinkDisabledRelocationPolicy<T> {
  return Object.freeze({ kind: 'disabled' }) as ZLinkDisabledRelocationPolicy<T>;
}

export function zlinkRecreateRelocation<T>(): ZLinkRecreateRelocationPolicy<T> {
  return Object.freeze({ kind: 'recreate' }) as ZLinkRecreateRelocationPolicy<T>;
}

export function zlinkSnapshotRelocation<TActor extends ZLinkActor>(
  adapterType: Type<ZLinkActorRelocationAdapter<TActor>>
): ZLinkSnapshotRelocationPolicy<TActor>;
export function zlinkSnapshotRelocation<TSpot extends ZLinkSpot | ZLinkInstanceSpot>(
  adapterType: Type<ZLinkSpotRelocationAdapter<TSpot>>
): ZLinkSnapshotRelocationPolicy<TSpot>;
export function zlinkSnapshotRelocation<T>(
  adapterType: Type
): ZLinkSnapshotRelocationPolicy<T> {
  return Object.freeze({ kind: 'snapshot', adapterType }) as ZLinkSnapshotRelocationPolicy<T>;
}

export interface ZLinkObjectPlacementIntent {
  readonly placementProfile?: ZLinkPlacementProfile;
  readonly affinityKey?: ZLinkAffinityKey;
}
