/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum ErrorCode {
    EFSM(156384763),
    ENOCOMPATPROTO(156384764),
    ETERM(156384765),
    EMTHREAD(156384766);

    private final int value;
    ErrorCode(int v) { this.value = v; }
    public int getValue() { return value; }
}
