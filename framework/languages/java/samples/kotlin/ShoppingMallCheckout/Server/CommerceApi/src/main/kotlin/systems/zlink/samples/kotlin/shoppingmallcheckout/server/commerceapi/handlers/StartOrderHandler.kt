package systems.zlink.samples.kotlin.shoppingmallcheckout.server.commerceapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.commerceapi.StartOrderUseCase
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.StartOrderReq
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.StartOrderRes

@ZLinkHandlerGroup("commerce")
class StartOrderHandler(
    private val useCase: StartOrderUseCase,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<StartOrderReq, StartOrderRes> {
    override fun handle(
        request: StartOrderReq,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        useCase.execute(request)
    }
}
