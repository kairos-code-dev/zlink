import { randomUUID } from 'node:crypto';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  type RoutingId,
  type SpotRef,
  type Type,
  type ZLinkAffinityKey,
  type ZLinkPlacementProfile,
  type ZLinkSpot,
  type ZLinkSpotCreateCall,
  type ZLinkSpotCreateResult,
  type ZLinkSpotGetOrCreateCall,
  type ZLinkSpotManager,
  ZLinkSpotKind
} from '../../contracts';
import type { ZLinkMessageSerializer } from '../../contracts';
import type { ZLinkObjectFactoryRegistration } from '../../contracts/Configuration/RegistrationTypes';
import { ZLinkConfigurationException } from '../configuration';
import type {
  ZLinkUserSpotCreationCoordinator
} from '../host/user-spot-creation-coordinator';
import {
  ZLinkUserSpotRidCollisionError
} from '../host/user-spot-creation-coordinator';
import type { ZLinkSpotRouteResolver } from './spot-routing-internal';
import type { DefaultZLinkSpotManager } from './index';
import { encodeFrameworkPayloadMessage } from '../messaging/payload-codec';

export interface ZLinkPublicSpotManagerOptions {
  readonly local: DefaultZLinkSpotManager;
  readonly coordinator: ZLinkUserSpotCreationCoordinator;
  readonly factories: ReadonlyMap<
    string,
    ReadonlyMap<string, ZLinkObjectFactoryRegistration<ZLinkSpot>>
  >;
  readonly resolver: () => ZLinkSpotRouteResolver | undefined;
  readonly isLocalNode: (meshName: string, nodeRid: RoutingId) => boolean;
  readonly defaultTimeoutMs: number;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly ridFactory?: () => RoutingId;
}

export class ZLinkPublicSpotManager implements ZLinkSpotManager {
  constructor(private readonly options: ZLinkPublicSpotManagerOptions) {}

  create(spotType: string): ZLinkSpotCreateCall {
    return this.call(this.newSpotRid(), spotType, true);
  }

  getOrCreate(spotRid: RoutingId, spotType: string): ZLinkSpotGetOrCreateCall {
    return this.call(spotRid, spotType, false);
  }

  async find(spotRid: RoutingId, signal?: AbortSignal): Promise<SpotRef | undefined> {
    const resolver = this.options.resolver();
    if (resolver === undefined) {
      throw new ZLinkConfigurationException('Spot lookup requires a Location Store.');
    }
    try {
      const route = await resolver.resolve(spotRid, signal);
      if (route.spotKind !== ZLinkSpotKind.User) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.SpotTypeMismatch,
          `Spot '${String(spotRid)}' is not a User Spot.`
        );
      }
      if (route.targetSpotGeneration === undefined) return undefined;
      return {
        spotRid,
        objectGeneration: route.targetSpotGeneration,
        meshName: route.routerChannelId,
        nodeRid: route.targetNodeRid
      };
    } catch (error) {
      if (
        error instanceof ZLinkFrameworkException
        && error.kind === ZLinkFrameworkErrorKind.SpotRouteNotFound
      ) {
        return undefined;
      }
      throw error;
    }
  }

  async close(spot: SpotRef, signal?: AbortSignal): Promise<boolean> {
    return await this.options.coordinator.close(
      spot,
      current => {
        if (!this.options.isLocalNode(current.meshName, current.nodeRid)) {
          throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.RequestFailed,
            'Remote User Spot close requires the generation-fenced internal close operation.'
          );
        }
        return this.options.local.close(current.meshName, current.spotRid, signal);
      },
      signal
    );
  }

  private call(
    spotRid: RoutingId,
    stableType: string,
    retryRidCollision: boolean
  ): ZLinkSpotCreateCall {
    requireText(stableType, 'User Spot type');
    const state: MutableCreateCall = {
      submitted: false,
      selected: new Set()
    };
    const call: ZLinkSpotCreateCall = {
      inMesh: (meshName) => {
        selectOnce(state, 'inMesh');
        state.meshName = requireText(meshName, 'Mesh name');
        return call;
      },
      request: (request) => {
        selectOnce(state, 'request');
        state.request = request;
        return call;
      },
      placementProfile: (profile) => {
        selectOnce(state, 'placementProfile');
        state.placementProfile = requireText(profile, 'Placement profile');
        return call;
      },
      affinityKey: (key) => {
        selectOnce(state, 'affinityKey');
        state.affinityKey = requireText(key, 'Affinity key');
        return call;
      },
      timeout: (timeoutMs) => {
        selectOnce(state, 'timeout');
        if (!Number.isSafeInteger(timeoutMs) || timeoutMs <= 0) {
          throw invalidConfiguration('Spot creation timeout must be positive.');
        }
        state.timeoutMs = timeoutMs;
        return call;
      },
      submit: (signal) => this.submit(
        spotRid,
        stableType,
        retryRidCollision,
        state,
        signal
      )
    };
    return call;
  }

  private async submit(
    spotRid: RoutingId,
    stableType: string,
    retryRidCollision: boolean,
    state: MutableCreateCall,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    if (state.submitted) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.AlreadySubmitted,
        'Spot creation call has already been submitted.'
      );
    }
    state.submitted = true;
    const encodedRequest = encodeFrameworkPayloadMessage(
      state.request ?? null,
      this.options.messageSerializers
    );
    const requestBytes = Buffer.from(encodedRequest.data());
    encodedRequest.close();
    const timeoutMs = state.timeoutMs ?? this.options.defaultTimeoutMs;
    const deadline = Date.now() + timeoutMs;
    let candidateRid = spotRid;
    do {
      const remainingMs = deadline - Date.now();
      if (remainingMs <= 0) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.DeadlineExceeded,
          'User Spot creation exhausted its end-to-end deadline.',
          true
        );
      }
      let coordinated;
      try {
        coordinated = await this.options.coordinator.getOrCreate({
          meshName: state.meshName,
          spotRid: candidateRid,
          stableType,
          placementProfile: state.placementProfile,
          affinityKey: state.affinityKey,
          requestPayload: requestBytes,
          timeoutMs: remainingMs,
          signal,
          retryRidCollision
        }, (target, deadlineSignal) => {
          const selected = selectFactory(this.options.factories, stableType, target.meshName);
          return this.options.local.getOrCreate(
            target.meshName,
            selected.registration.implementation as Type<ZLinkSpot>,
            candidateRid,
            state.request,
            deadlineSignal
          );
        }, async (cleanupSignal) => {
          const meshName = state.meshName ?? [...this.options.factories]
            .find(([, byType]) => byType.has(stableType))?.[0];
          if (meshName !== undefined) {
            await this.options.local.close(meshName, candidateRid, cleanupSignal);
          }
        });
      } catch (error) {
        if (!retryRidCollision || !(error instanceof ZLinkUserSpotRidCollisionError)) {
          throw error;
        }
        candidateRid = this.newSpotRid();
        continue;
      }
      if (
        !retryRidCollision
        || coordinated.result.state !== 'existing'
      ) {
        return coordinated.result;
      }
      candidateRid = this.newSpotRid();
    } while (Date.now() < deadline);
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.DeadlineExceeded,
      'User Spot creation exhausted its end-to-end deadline.',
      true
    );
  }

  private newSpotRid(): RoutingId {
    return this.options.ridFactory?.() ?? `spot-${randomUUID()}` as RoutingId;
  }
}

interface MutableCreateCall {
  submitted: boolean;
  readonly selected: Set<string>;
  meshName?: string;
  request?: unknown;
  placementProfile?: ZLinkPlacementProfile;
  affinityKey?: ZLinkAffinityKey;
  timeoutMs?: number;
}

function selectOnce(state: MutableCreateCall, option: string): void {
  if (state.submitted) {
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.AlreadySubmitted,
      'Spot creation call has already been submitted.'
    );
  }
  if (state.selected.has(option)) {
    throw invalidConfiguration(`Spot creation option '${option}' was already selected.`);
  }
  state.selected.add(option);
}

function selectFactory(
  factories: ZLinkPublicSpotManagerOptions['factories'],
  stableType: string,
  selectedMesh?: string
): {
  readonly meshName: string;
  readonly registration: ZLinkObjectFactoryRegistration<ZLinkSpot>;
} {
  const matches = [...factories]
    .filter(([meshName]) => selectedMesh === undefined || meshName === selectedMesh)
    .flatMap(([meshName, byType]) => {
      const registration = byType.get(stableType);
      return registration === undefined ? [] : [{ meshName, registration }];
    });
  if (matches.length !== 1) {
    throw invalidConfiguration(
      matches.length === 0
        ? `User Spot type '${stableType}' is not registered in the selected mesh.`
        : `User Spot type '${stableType}' is registered in multiple meshes; call inMesh(...).`
    );
  }
  return matches[0]!;
}

function requireText(value: string, label: string): string {
  const bytes = Buffer.byteLength(value);
  if (bytes < 1 || bytes > 255 || value.includes('\0')) {
    throw invalidConfiguration(`${label} must contain 1..255 UTF-8 bytes and no NUL.`);
  }
  return value;
}

function invalidConfiguration(message: string): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.InvalidConfiguration,
    message
  );
}
