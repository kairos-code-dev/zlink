package main

func multiTopicMatches(buffer []byte, topicLen int, expected string) bool {
	if topicLen != len(expected) || topicLen > len(buffer) {
		return false
	}
	for i := 0; i < topicLen; i++ {
		if buffer[i] != expected[i] {
			return false
		}
	}
	return true
}
