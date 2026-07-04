import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { courierActorNodeRid } from '../../Shared/Configuration/sample-names';
import { actorRefForMessage, PacketNames } from '../../Shared/Contracts/messages';
import type { RoutingId, ZLinkRequestContext, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { BindCourierReq, BindCourierRes } from '../../Shared/Contracts/messages';

@zlinkRequestHandler('courier-gateway', PacketNames.bindCourier)
class BindCourierHandler implements ZLinkRequestHandler<BindCourierReq, BindCourierRes> {
  async handle(request: BindCourierReq, context: ZLinkRequestContext): Promise<BindCourierRes> {
    void context;
    const nodeRid = courierActorNodeRid(request.courierId);
    console.error(`deliverydispatch courier-gateway: bound courier=${request.courierId} node=${nodeRid} session=${request.sessionRoute}`);
    return {
      courierId: request.courierId,
      actor: actorRefForMessage({
        nodeRid: nodeRid as unknown as RoutingId,
        actorId: request.courierId,
        generation: 1n
      }),
      sessionRoute: request.sessionRoute
    };
  }
}

export { BindCourierHandler };
