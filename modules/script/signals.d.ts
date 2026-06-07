// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

export interface SignalMethods {
    connect(name: string, handler: (self: object, ...args: any[]) => any): number;
    connectAfter(name: string, handler: (self: object, ...args: any[]) => any): number;
    disconnect(id: number): void;
    disconnectAll(): void;
    emit(name: string, ...args: any[]): void;
    signalHandlerIsConnected(id: number): boolean;
}

export declare function addSignalMethods<T>(proto: T): asserts proto is T & SignalMethods;
