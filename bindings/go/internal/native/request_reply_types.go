// SPDX-License-Identifier: MPL-2.0

package native

import "time"

const defaultRequestTimeout = 5 * time.Second

type RequestReplyCallback func(RequestResult, []*Message)

type RequestReplyCompletion struct {
	Result RequestResult
	Parts  []*Message
	Err    error
}

type requestResult struct {
	result RequestResult
	parts  []*Message
}
