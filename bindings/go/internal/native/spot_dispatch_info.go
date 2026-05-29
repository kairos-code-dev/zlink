// SPDX-License-Identifier: MPL-2.0

package native

/*
#include "zlink.h"
*/
import "C"

import "unsafe"

type SpotDispatchEvent int

const (
	SpotDispatchEventSubscribeReadable      SpotDispatchEvent = 1
	SpotDispatchEventRoutedReadable         SpotDispatchEvent = 2
	SpotDispatchEventTimerReadable          SpotDispatchEvent = 3
	SpotDispatchEventChannelReplyReadable   SpotDispatchEvent = 4
	SpotDispatchEventActorReadable          SpotDispatchEvent = 5
	SpotDispatchEventActorJoinReadable      SpotDispatchEvent = 6
	SpotDispatchEventActorLifecycleReadable SpotDispatchEvent = 7
)

type SpotDispatchSubjectKind int

const (
	SpotDispatchSubjectSpot          SpotDispatchSubjectKind = 1
	SpotDispatchSubjectTimer         SpotDispatchSubjectKind = 2
	SpotDispatchSubjectChannelDealer SpotDispatchSubjectKind = 3
	SpotDispatchSubjectActor         SpotDispatchSubjectKind = 4
)

type SpotDispatchInfo struct {
	Event         SpotDispatchEvent
	SubjectKind   SpotDispatchSubjectKind
	Timer         *Timer
	ChannelDealer *DealerSocket
	Actor         *ActorRef
	nodeHandle    unsafe.Pointer
}

func (i *SpotDispatchInfo) RecvActorPart(flags RecvFlags) (*ActorPart, error) {
	if i.Event != SpotDispatchEventActorReadable || i.SubjectKind != SpotDispatchSubjectActor {
		return nil, &RecvError{Result: RecvNotSupported, internalErrno: int(C.ENOTSUP)}
	}
	if i.Actor == nil {
		return nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return recvActorPart(i.nodeHandle, *i.Actor, flags)
}
