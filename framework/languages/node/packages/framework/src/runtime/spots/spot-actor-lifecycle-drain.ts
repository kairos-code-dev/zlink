import type { ZLinkActor, ZLinkActorMembership } from '../../contracts';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendSpot
} from '../backend/contracts';
import type { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import { ZLINK_RECV_DONT_WAIT } from './spot-native-flags';
import { createActorMembership } from '../actors/actor-lifecycle-snapshot';

interface ZLinkSpotActorLifecycleTarget {
  onJoinedActor?(actor: ZLinkActorMembership): Promise<void> | void;
  onLeaveActor?(actor: ZLinkActorMembership): Promise<void> | void;
  onDisconnectActor?(actor: ZLinkActorMembership): Promise<void> | void;
}

interface ZLinkSpotActorLifecycleDrainOptions {
  readonly nativeSpot: ZLinkBackendSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly resolveActor: (actorId: string) => ZLinkActor | undefined;
  readonly getTarget: () => ZLinkSpotActorLifecycleTarget;
  readonly commitActorDeparture?: (actorId: string) => void;
  readonly waitIdle: () => Promise<void>;
}

const ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED = 1;
const ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT = 2;
const ZLINK_SPOT_ACTOR_LIFECYCLE_DISCONNECTED = 3;

interface ZLinkSpotActorLifecycleEvent {
  readonly kind: number;
  readonly info: {
    readonly previousActor?: ZLinkBackendActorRef | null;
    readonly currentActor?: ZLinkBackendActorRef | null;
    readonly previousMembershipEpoch?: bigint;
    readonly currentMembershipEpoch?: bigint;
  };
}

export class ZLinkSpotActorLifecycleDrain {
  constructor(private readonly options: ZLinkSpotActorLifecycleDrainOptions) {}

  async drain(): Promise<void> {
    for (;;) {
      const event = this.options.nativeSpot.recvActorLifecycle(
        ZLINK_RECV_DONT_WAIT
      ) as ZLinkSpotActorLifecycleEvent | null;
      if (event === null) {
        await this.options.waitIdle();
        return;
      }
      const actorRef = event.kind === ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT
        ? event.info.previousActor
        : event.info.currentActor;
      const actorId = actorRef?.actorId;
      if (actorId === undefined || this.options.resolveActor(actorId) === undefined) {
        continue;
      }
      if (actorRef === null || actorRef === undefined) continue;
      await this.options.serial.execute(() => {
        const actor = this.options.resolveActor(actorId);
        if (actor === undefined) {
          return undefined;
        }
        const target = this.options.getTarget();
        const membershipEpoch = event.kind === ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT
          ? event.info.previousMembershipEpoch
          : event.info.currentMembershipEpoch;
        const membership = createActorMembership(
          actor,
          {
            actorId: actorRef.actorId,
            objectGeneration: actorRef.generation,
            meshName: actor.context.meshName,
            nodeRid: actorRef.nodeRid
          },
          membershipEpoch
        );
        if (event.kind === ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED) {
          return target.onJoinedActor?.(membership);
        }
        if (event.kind === ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT) {
          return Promise.resolve(target.onLeaveActor?.(membership)).then(() => {
            this.options.commitActorDeparture?.(actorId);
          });
        }
        if (event.kind === ZLINK_SPOT_ACTOR_LIFECYCLE_DISCONNECTED) {
          return target.onDisconnectActor?.(membership);
        }
        return undefined;
      });
    }
  }
}
