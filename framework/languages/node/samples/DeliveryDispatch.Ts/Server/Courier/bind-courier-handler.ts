import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { courierActorNodeRid } from '../../Shared/Configuration/sample-names';
import { PacketNames } from '../../Shared/Contracts/messages';
import type { ZLinkRequestContext, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { BindCourierReq, BindCourierRes } from '../../Shared/Contracts/messages';

@zlinkRequestHandler('courier-gateway', PacketNames.bindCourier)
class BindCourierHandler implements ZLinkRequestHandler<BindCourierReq, BindCourierRes> {
  async handle(request: BindCourierReq, context: ZLinkRequestContext): Promise<BindCourierRes> {
    void context;
    const nodeRid = courierActorNodeRid(request.courierId);
    console.error(`deliverydispatch courier-gateway: bound courier=${request.courierId} node=${nodeRid} session=${request.sessionRoute}`);
    return {
      courierId: request.courierId,
      actor: {
        nodeRid,
        actorId: request.courierId,
        generation: 1
      },
      sessionRoute: request.sessionRoute
    };
  }
}

export { BindCourierHandler };
