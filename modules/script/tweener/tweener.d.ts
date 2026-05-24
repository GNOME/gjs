// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

interface TweenParameters {
    delay?: number;
    max?: number;
    min?: number;
    onComplete?: Function;
    onStart?: Function;
    onUpdate?: Function;
    time: number;
    transition?: "linear";
}

type CallerParameters = TweenParameters & {
    count: number;
    waitForFrames?: number;
}

export interface FrameTickerInterface {
    FRAME_RATE: number;
    getTime(): number;
    start(): void;
    stop(): void;

    connect(name: 'prepare-frame', handler: (self: FrameTickerInterface) => void): number;
    disconnect(id: number): void;
    emit(name: 'prepare-frame'): void;
}

export declare class FrameTicker implements FrameTickerInterface {
    FRAME_RATE: number;

    _init(): void;
    getTime(): number;
    start(): void;
    stop(): void;

    connect(name: string, handler: (self: FrameTicker, ...args: any[]) => any): number;
    connectAfter(name: string, handler: (self: FrameTicker, ...args: any[]) => any): number;
    disconnect(id: number): void;
    disconnectAll(): void;
    emit(name: string, ...args: any[]): void;
    signalHandlerIsConnected(id: number): boolean;
}

export declare function addCaller(target: object, params: CallerParameters): void;
export declare function addTween(
    target: object,
    params: TweenParameters & Record<string, any>,
): void;
export declare function getTweenCount(target: object): number;
/** @deprecated Don't use this. It affects the whole application. */
export declare function pauseAllTweens(): void;
export declare function pauseTweens(target: object, ...tweens: string[]): void;
export declare function registerSpecialProperty<T extends object, P>(
    name: string,
    getFunction: (target: T, parameters: P) => number,
    setFunction: (target: T, value: number, parameters: P) => void,
    parameters?: P,
    preprocessFunction?: (target: T, parameters: P) => number,
): void;
export declare function registerSpecialPropertyModifier(
    name: string,
    modifyFunction: (props: string[]) => { name: string; parameters: any }[],
    getFunction: (begin: number, end: number, time: number) => number,
): void;
export declare function registerSpecialPropertySplitter<P>(
    name: string,
    splitFunction: (val: number, parameters: P) => { name: string, value: number }[],
    parameters?: P,
): void;
/** @deprecated Don't use this. It affects the whole application. */
export declare function removeAllTweens(): void;
export declare function removeTweens(target: object, ...tweens: string[]): void;
/** @deprecated Don't use this. It affects the whole application. */
export declare function resumeAllTweens(): void;
export declare function resumeTweens(target: object, ...tweens: string[]): void;
export declare function setFrameTicker(ticker: FrameTickerInterface): void;
