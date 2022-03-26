// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

class Connection {
    #instance;
    #name;
    #callback;
    #disconnected;

    /**
     * @param {object} params _
     * @param {EventEmitter} params.instance the instance the connection is connected to
     * @param {string} params.name the name of the signal
     * @param {Function} params.callback the callback for the signal
     * @param {boolean} params.disconnected whether the connection is disconnected
     */
    constructor({instance, name, callback, disconnected = false}) {
        this.#instance = instance;
        this.#name = name;
        this.#callback = callback;
        this.#disconnected = disconnected;
    }

    matches(callback) {
        return this.#callback === callback;
    }

    disconnect() {
        this.#instance.disconnect(this);
    }

    trigger(...args) {
        this.#callback.apply(null, [this.#instance, ...args]);
    }

    get name() {
        return this.#name;
    }

    set disconnected(value) {
        if (!value)
            throw new Error('Connections cannot be re-connected.');

        this.#disconnected = value;
    }

    get disconnected() {
        return this.#disconnected;
    }
}

const sEmit = Symbol('private emit');

export class EventEmitter {
    /** @type {Connection[]} */
    #signalConnections = [];

    /**
     * @param {string} name
     * @param {(...args: any[]) => any} callback
     * @returns {Connection}
     */
    connect(name, callback) {
        // be paranoid about callback arg since we'd start to throw from emit()
        // if it was messed up
        if (typeof callback !== 'function') {
            throw new Error(
                'When connecting signal must give a callback that is a function'
            );
        }

        const connection = new Connection({
            instance: this,
            name,
            callback,
        });

        // this makes it O(n) in total connections to emit, but I think
        // it's right to optimize for low memory and reentrancy-safety
        // rather than speed
        this.#signalConnections.push(connection);

        return connection;
    }

    /**
     * @param {Connection} connection the connection returned by {@link connect}
     */
    disconnect(connection) {
        if (connection.disconnected) {
            throw new Error(
                `Signal handler for ${connection.name} already disconnected`
            );
        }

        const index = this.#signalConnections.indexOf(connection);
        if (index !== -1) {
            // Mark the connection as disconnected.
            connection.disconnected = true;

            this.#signalConnections.splice(index, 1);
            return;
        }

        throw new Error('No signal connection found for connection');
    }

    /**
     * @param {Connection} connection the connection returned by {@link connect}
     * @returns {boolean} whether the signal connection is connected
     */
    signalHandlerIsConnected(connection) {
        const index = this.#signalConnections.indexOf(connection);
        return index !== -1 && !connection.disconnected;
    }

    /**
     * @param {string} name
     * @param {(...args: any[]) => any} handler
     * @returns {Connection | undefined}
     */
    findConnectionBySignalHandler(name, handler) {
        return this.#signalConnections.find(
            connection =>
                connection.name === name && connection.matches(handler)
        );
    }

    disconnectAll() {
        while (this.#signalConnections.length > 0)
            this.#signalConnections[0].disconnect();
    }

    /**
     * @param {string} name the signal name to emit
     * @param {...any} args the arguments to pass
     */
    emit(name, ...args) {
        // To deal with re-entrancy (removal/addition while
        // emitting), we copy out a list of what was connected
        // at emission start; and just before invoking each
        // handler we check its disconnected flag.
        let handlers = [];
        let i;
        let length = this.#signalConnections.length;
        for (i = 0; i < length; ++i) {
            let connection = this.#signalConnections[i];
            if (connection.name === name)
                handlers.push(connection);
        }

        length = handlers.length;
        for (i = 0; i < length; ++i) {
            let connection = handlers[i];

            if (!connection.disconnected) {
                try {
                    // since we pass "null" for this, the global object will be used.
                    let ret = connection.trigger(...args);

                    // if the callback returns true, we don't call the next
                    // signal handlers
                    if (ret === true)
                        break;
                } catch (e) {
                    // just log any exceptions so that callbacks can't disrupt
                    // signal emission
                    console.error(
                        `Exception in callback for signal: ${name}\n`,
                        e
                    );
                }
            }
        }
    }
}

export class Event {
    /**
     * The event is not being processed at this time.
     */
    static NONE = 0;

    /**
     * The event is being propagated through the target's ancestor objects. This process starts with the Window, then Document, then the HTMLHtmlElement, and so on through the elements until the target's parent is reached. Event listeners registered for capture mode when EventTarget.addEventListener() was called are triggered during this phase.
     */
    static CAPTURING_PHASE = 1;

    /**
     * The event has arrived at the event's target. Event listeners registered for this phase are called at this time. If Event.bubbles is false, processing the event is finished after this phase is complete.
     */
    static AT_TARGET = 2;

    /**
     * The event is propagating back up through the target's ancestors in reverse order, starting with the parent, and eventually reaching the containing Window. This is known as bubbling, and occurs only if Event.bubbles is true. Event listeners registered for this phase are triggered during this process.
     */
    static BUBBLING_PHASE = 3;

    static {
        const descriptor = {
            enumerable: true,
            configurable: false,
            writable: false,
        };

        Object.defineProperties(this, {
            NONE: {...descriptor, value: this.NONE},
            CAPTURING_PHASE: {...descriptor, value: this.CAPTURING_PHASE},
            AT_TARGET: {...descriptor, value: this.AT_TARGET},
            BUBBLING_PHASE: {...descriptor, value: this.BUBBLING_PHASE},
        });
    }

    #type;
    #bubbles;
    #cancelable;
    #composed;
    #target;
    #phase = Event.NONE;
    #timestamp = Date.now();

    constructor(typeArg, eventInit = {}) {
        const {
            bubbles = false,
            cancelable = false,
            composed = false,
        } = eventInit;

        this.#type = typeArg;
        this.#bubbles = bubbles;
        this.#cancelable = cancelable;
        this.#composed = composed;
    }

    cancelBubble = false;

    get bubbles() {
        return this.#bubbles;
    }

    get cancelable() {
        return this.#cancelable;
    }

    get composed() {
        return this.#composed;
    }

    get timeStamp() {
        return this.#timestamp;
    }

    get target() {
        return this.#target;
    }

    get currentTarget() {
        return this.target;
    }

    get defaultPrevented() {
        return false;
    }

    get isTrusted() {
        return false;
    }

    get type() {
        return this.#type;
    }

    get eventPhase() {
        return this.#phase;
    }


    composedPath() {
        return this.target ? [this.target] : [];
    }

    preventDefault() { }
    stopImmediatePropagation() { }
    stopPropagation() { }

    [sEmit](target) {
        this.#target = target;
        this.#phase = Event.AT_TARGET;
    }
}

export class MessageEvent extends Event {
    #data;
    #source;
    #ports;
    #lastEventId;

    constructor(type, init) {
        const {data = null, source = null, ports = [], lastEventId = '', ...eventInit} = init;
        super(type, eventInit);

        this.#data = data;
        this.#source = source;
        this.#ports = ports;
        this.#lastEventId = lastEventId;
    }

    get data() {
        return this.#data;
    }

    get source() {
        return this.#source;
    }

    get ports() {
        return this.#ports;
    }

    get lastEventId() {
        return this.#lastEventId;
    }
}

export class EventTarget {
    #emitter = new EventEmitter();
    #handlers = new Map();

    /**
     * Registers an event handler of a specific event type on the EventTarget.
     *
     * @param type
     * @param listener
     */
    addEventListener(type, listener) {
        const wrapper = this.#handlers.get(listener) ?? function (instance, event) {
            listener(event);
        };
        this.#handlers.set(listener, wrapper);
        this.#emitter.connect(type, wrapper);
    }

    /**
     * Removes an event listener from the EventTarget.
     *
     * @param type
     * @param listener
     */
    removeEventListener(type, listener) {
        const wrapper = this.#handlers.get(listener);

        if (!wrapper)
            return;

        const connection = this.#emitter.findConnectionBySignalHandler(
            type,
            wrapper
        );

        connection?.disconnect();
        this.#handlers.delete(listener);
    }

    /**
     * Dispatches an event to this EventTarget.
     *
     * @param {Event} event the event to dispatch
     */
    dispatchEvent(event) {
        if (event instanceof Event) {
            event[sEmit](this);
            this.#emitter.emit(event.type, event);
        }

        return true;
    }
}


Object.defineProperty(globalThis, 'Event', {
    configurable: false,
    enumerable: true,
    writable: true,
    value: Event,
});

Object.defineProperty(globalThis, 'EventTarget', {
    configurable: false,
    enumerable: true,
    writable: true,
    value: EventTarget,
});
