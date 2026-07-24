import { randomUUID } from 'node:crypto';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  type RoutingId,
  type SpotRef,
  type Type,
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
import type {
  ServiceUserSpotCloseRecord
} from '../foundation/service-stateful-wire-codec';
import type {
  ServiceUserSpotOperationResult
} from '../foundation/service-stateful-runtime';

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
  readonly remoteClose?: (
    meshName: string,
    targetNodeRid: string,
    request: Omit<ServiceUserSpotCloseRecord, 'kind' | 'correlation' | 'operation'>,
    timeoutMs: number
  ) => Promise<ServiceUserSpotOperationResult>;
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
    const resolved = await this.options.coordinator.resolveCloseTarget(spot, signal);
    if (resolved === undefined) return false;
    const current = resolved.spot;
    if (this.options.isLocalNode(current.meshName, current.nodeRid)) {
      return await this.options.coordinator.close(
        spot,
        (local) => this.options.local.close(local.meshName, local.spotRid, signal),
        signal,
        (local) => this.options.local.hasActiveSpot(local.spotRid),
        (local) =>
          this.options.local.canCloseUserSpot?.(local.meshName, local.spotRid)
          ?? true
      );
    }
    if (this.options.remoteClose === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestFailed,
        'Remote User Spot close transport is not configured.'
      );
    }
    const timeoutMs = this.options.defaultTimeoutMs;
    const snapshot = resolved.snapshot;
    const result = await this.options.remoteClose(
      current.meshName,
      String(current.nodeRid),
      {
        sourceNodeRid: '',
        sourceNodeGeneration: 1n,
        target: {
          spotRid: String(current.spotRid),
          objectGeneration: current.objectGeneration,
          targetNodeRid: String(current.nodeRid),
          targetNodeGeneration: snapshot.allocation.descriptorLifecycleGeneration,
          authorityOwnerGeneration: snapshot.authorityOwnerGeneration,
          expectedStoreVersion: snapshot.storeVersion.value
        },
        deadlineUnixMs: BigInt(Date.now() + timeoutMs)
      },
      timeoutMs
    );
    if (
      result.terminalResult !== 0
      || result.tail?.kind !== 'userSpotClose'
    ) {
      const kind = result.failureCode === 33
        ? ZLinkFrameworkErrorKind.SpotGenerationStale
        : result.failureCode === 34
          ? ZLinkFrameworkErrorKind.SpotMoving
          : result.terminalResult === 101
            ? ZLinkFrameworkErrorKind.DeadlineExceeded
            : ZLinkFrameworkErrorKind.RequestFailed;
      throw new ZLinkFrameworkException(
        kind,
        `Remote User Spot close failed. terminalResult=${result.terminalResult}, failureCode=${result.failureCode}.`,
        kind === ZLinkFrameworkErrorKind.SpotMoving
          || kind === ZLinkFrameworkErrorKind.DeadlineExceeded
      );
    }
    return result.tail.closed;
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
          requestPayload: requestBytes,
          timeoutMs: remainingMs,
          signal,
          retryRidCollision
        }, async (target, deadlineSignal) => {
          const selected = selectFactory(this.options.factories, stableType, target.meshName);
          this.options.local.beginUserSpotPublication?.(target.meshName, candidateRid);
          try {
            const result = await this.options.local.getOrCreate(
              target.meshName,
              selected.registration.implementation as Type<ZLinkSpot>,
              candidateRid,
              state.request,
              deadlineSignal
            );
            return {
              ...result,
              publication: {
                publish: () => this.options.local.publishUserSpot?.(
                  target.meshName,
                  candidateRid
                ),
                abort: () => this.options.local.abortUserSpotPublication?.(
                  target.meshName,
                  candidateRid
                )
              }
            };
          } catch (error) {
            this.options.local.abortUserSpotPublication?.(target.meshName, candidateRid);
            throw error;
          }
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
