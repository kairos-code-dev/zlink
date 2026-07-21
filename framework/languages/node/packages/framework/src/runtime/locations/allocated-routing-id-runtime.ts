import {
  zlinkRuntimeDefaultLocationOptions,
  type ZLinkLocationOptionOverrides
} from '../../contracts/Locations/Options';
import {
  type ZLinkAllocatedRoutingId,
  type ZLinkAllocatedRoutingIdProvider,
  type ZLinkRoutingIdSlotAllocation,
  type ZLinkRoutingIdSlotAllocationMember,
  type ZLinkRoutingIdSlotAllocationStore
} from '../../contracts/Locations';
import { ZLinkConfigurationException } from '../../contracts/Configuration/ConfigurationException';
import {
  collectRoutingIdAllocationMembers,
  type ZLinkRoutingIdAllocationMemberRegistration
} from '../../contracts/Configuration/RoutingIdAllocationRegistration';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type { ZLinkLocationRuntime } from './runtime';

export class ZLinkAllocatedRoutingIdRuntime implements ZLinkAllocatedRoutingIdProvider {
  private readonly options: Required<ZLinkLocationOptionOverrides>;
  private readonly groups: readonly AllocationGroup[];
  private readonly ready = new Map<string, Deferred<ZLinkAllocatedRoutingId>>();
  private acquired: AcquiredGroup[] = [];
  private monitor?: ReturnType<typeof setInterval>;
  private fenceDeadlineMs = Number.POSITIVE_INFINITY;
  private started = false;
  private fenced = false;
  private readyPublished = false;

  constructor(
    registration: ZLinkFrameworkRegistration,
    private readonly store: ZLinkRoutingIdSlotAllocationStore,
    private readonly locations: ZLinkLocationRuntime,
    private readonly fencingRequired: (error: Error) => void
  ) {
    this.options = { ...zlinkRuntimeDefaultLocationOptions, ...registration.locations.options };
    this.groups = buildGroups(registration);
    for (const group of this.groups) this.ready.set(group.name, deferred());
  }

  get enabled(): boolean {
    return this.groups.length > 0;
  }

  async start(signal?: AbortSignal): Promise<void> {
    if (!this.enabled || this.started) return;
    this.started = true;
    this.fenced = false;
    try {
      for (;;) {
        const acquired: AcquiredGroup[] = [];
        let retry = false;
        try {
          for (const group of this.groups) {
            let result;
            try {
              result = await this.store.acquireRoutingIdSlot({
                groupName: group.name,
                members: group.storeMembers,
                slotCount: group.slotCount,
                ownerId: this.locations.ownerId,
                leaseTtlMs: this.options.ownerLeaseTtlMs
              }, signal);
            } catch (error) {
              if (signal?.aborted === true) throw error;
              retry = true;
              break;
            }
            if (result.kind === 'acquired') {
              acquired.push({ group, allocation: result.allocation });
              this.confirmLease(result.allocation);
              continue;
            }
            if (result.kind === 'groupExhausted') {
              retry = true;
              break;
            }
            if (result.kind === 'groupConfigurationMismatch') {
              throw new ZLinkConfigurationException(
                `Routing-id allocation group '${group.name}' does not match its stored configuration.`
              );
            }
            throw new ZLinkConfigurationException(
              `Routing-id allocation group '${group.name}' conflicts with fixed routing-id peers.`
            );
          }
          if (!retry) {
            for (const item of acquired) {
              for (const member of item.group.members) {
                member.apply(`${member.routingIdPrefix}${item.allocation.slot}`);
              }
            }
            this.acquired = acquired;
            this.startFenceMonitor();
            return;
          }
        } catch (error) {
          await releaseGroups(this.store, acquired);
          throw error;
        }
        await releaseGroups(this.store, acquired).catch(() => undefined);
        await wait(this.options.pollingIntervalMs, signal);
      }
    } catch (error) {
      this.started = false;
      throw error;
    }
  }

  markReady(): void {
    this.readyPublished = true;
    for (const item of this.acquired) {
      this.ready.get(item.group.name)?.resolve({
        groupName: item.group.name,
        slot: item.allocation.slot,
        memberRoutingIds: new Map(item.group.members.map((member) => [
          member.memberName,
          `${member.routingIdPrefix}${item.allocation.slot}`
        ]))
      });
    }
  }

  async stop(signal?: AbortSignal): Promise<void> {
    if (!this.started) return;
    this.started = false;
    this.stopFenceMonitor();
    const acquired = this.acquired;
    this.acquired = [];
    if (!this.readyPublished) {
      this.fail(new Error('Routing-id allocation stopped before readiness.'));
    }
    await releaseGroups(this.store, acquired, signal);
  }

  fail(error: unknown): void {
    for (const result of this.ready.values()) result.reject(error);
  }

  async waitForReadyAllocation(groupName: string, signal?: AbortSignal): Promise<ZLinkAllocatedRoutingId> {
    const result = this.ready.get(groupName);
    if (result === undefined) {
      throw new ZLinkConfigurationException(
        `Routing-id allocation group '${groupName}' is not registered.`
      );
    }
    return await withAbort(result.promise, signal);
  }

  private startFenceMonitor(): void {
    this.locations.addOwnerLeaseRenewedHandler(this.ownerLeaseRenewed);
    const intervalMs = Math.max(10, Math.min(250, Math.floor(this.options.routingIdFencingMarginMs / 4)));
    this.monitor = setInterval(() => this.checkFenceDeadline(), intervalMs);
    this.monitor.unref();
  }

  private stopFenceMonitor(): void {
    this.locations.removeOwnerLeaseRenewedHandler(this.ownerLeaseRenewed);
    if (this.monitor !== undefined) clearInterval(this.monitor);
    this.monitor = undefined;
  }

  private readonly ownerLeaseRenewed = (renewal: { readonly leaseExpiresAt: Date; readonly storeNow: Date }): void => {
    this.checkFenceDeadline();
    if (this.fenced) return;
    this.confirmLease(renewal);
  };

  private confirmLease(lease: { readonly leaseExpiresAt: Date; readonly storeNow: Date }): void {
    const safeForMs = lease.leaseExpiresAt.getTime()
      - lease.storeNow.getTime()
      - this.options.routingIdFencingMarginMs;
    if (safeForMs <= 0) {
      throw new ZLinkConfigurationException(
        'The allocated routing-id owner lease has no positive fencing interval.'
      );
    }
    this.fenceDeadlineMs = monotonicMs() + safeForMs;
  }

  private checkFenceDeadline(): void {
    if (this.fenced || monotonicMs() < this.fenceDeadlineMs) return;
    this.fenced = true;
    const error = new Error(
      'The allocated routing-id owner lease could not be renewed before its fencing deadline.'
    );
    for (const result of this.ready.values()) result.reject(error);
    this.fencingRequired(error);
  }
}

interface AllocationGroup {
  readonly name: string;
  readonly slotCount: number;
  readonly members: readonly ZLinkRoutingIdAllocationMemberRegistration[];
  readonly storeMembers: readonly ZLinkRoutingIdSlotAllocationMember[];
}

interface AcquiredGroup {
  readonly group: AllocationGroup;
  readonly allocation: ZLinkRoutingIdSlotAllocation;
}

function buildGroups(registration: ZLinkFrameworkRegistration): readonly AllocationGroup[] {
  const grouped = new Map<string, ZLinkRoutingIdAllocationMemberRegistration[]>();
  for (const member of collectRoutingIdAllocationMembers(registration)) {
    const group = grouped.get(member.groupName) ?? [];
    group.push(member);
    grouped.set(member.groupName, group);
  }
  return [...grouped]
    .sort(([left], [right]) => left.localeCompare(right))
    .map(([name, members]) => {
      members.sort((left, right) => left.memberName.localeCompare(right.memberName));
      return {
        name,
        slotCount: members[0]?.slotCount ?? 0,
        members,
        storeMembers: members.map((member) => ({
          meshName: member.memberName,
          routingIdPrefix: member.routingIdPrefix
        }))
      };
    });
}

async function releaseGroups(
  store: ZLinkRoutingIdSlotAllocationStore,
  groups: readonly AcquiredGroup[],
  signal?: AbortSignal
): Promise<void> {
  const errors: unknown[] = [];
  for (const item of [...groups].reverse()) {
    try {
      await store.releaseRoutingIdSlot(
        item.group.name,
        item.allocation.slot,
        item.allocation.owner,
        signal
      );
    } catch (error) {
      errors.push(error);
    }
  }
  if (errors.length === 1) throw errors[0];
  if (errors.length > 1) throw new AggregateError(errors, 'Routing-id allocation release failed.');
}

function wait(delayMs: number, signal?: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    if (signal?.aborted === true) {
      reject(signal.reason);
      return;
    }
    const timer = setTimeout(() => {
      signal?.removeEventListener('abort', abort);
      resolve();
    }, delayMs);
    const abort = () => {
      clearTimeout(timer);
      reject(signal?.reason);
    };
    signal?.addEventListener('abort', abort, { once: true });
  });
}

function monotonicMs(): number {
  return Number(process.hrtime.bigint() / 1_000_000n);
}

interface Deferred<T> {
  readonly promise: Promise<T>;
  resolve(value: T): void;
  reject(error: unknown): void;
}

function deferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void;
  let reject!: (error: unknown) => void;
  const promise = new Promise<T>((accepted, failed) => {
    resolve = accepted;
    reject = failed;
  });
  promise.catch(() => undefined);
  return { promise, resolve, reject };
}

function withAbort<T>(operation: Promise<T>, signal?: AbortSignal): Promise<T> {
  if (signal === undefined) return operation;
  if (signal.aborted) return Promise.reject(signal.reason);
  return new Promise<T>((resolve, reject) => {
    const aborted = () => reject(signal.reason);
    signal.addEventListener('abort', aborted, { once: true });
    operation.then(resolve, reject).finally(() => {
      signal.removeEventListener('abort', aborted);
    }).catch(() => undefined);
  });
}
