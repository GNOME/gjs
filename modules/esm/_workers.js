
const {NativeWorker, setReceiver, getName} = import.meta.importSync('_workerNative');

import {MessageEvent, EventTarget} from './events.js';

function messageReceiver(source, message) {
    const event = new MessageEvent('message', {
        source,
        data: message,
    });

    source.dispatchEvent(event);
}

class Worker extends EventTarget {
    #nativeWorker;

    constructor(uri, options = {}) {
        super();

        const {type = 'module', name} = options;

        if (type !== 'module')
            throw Error('Workers must be of type "module"');

        this.#nativeWorker = new NativeWorker(uri, name ?? null);
        this.#nativeWorker.setReceiver((...args) => messageReceiver(this, ...args));
    }

    #onerror = null;

    set onerror(handler) {
        if (this.#onerror)
            this.removeEventListener('error', this.#onerror);
        this.addEventListener('error', this.#onerror = handler);
    }

    #onmessage = null;

    set onmessage(handler) {
        if (this.#onmessage)
            this.removeEventListener('message', this.#onmessage);
        this.addEventListener('message', this.#onmessage = handler);
    }

    #onmessageerror = null;

    set onmessageerror(handler) {
        if (this.#onmessageerror)
            this.removeEventListener('messageerror', this.#onmessageerror);
        this.addEventListener('messageerror', this.#onmessageerror = handler);
    }

    // eslint-disable-next-line no-unused-vars
    postMessage(message, transfer = []) {
        this.#nativeWorker.write(message);
    }

    terminate() {
        this.#nativeWorker.exit();
    }
}

// Using `imports` as a flag for the WorkerGlobal for now.
if ('imports' in globalThis) {
    Object.defineProperty(globalThis, 'Worker', {
        configurable: false,
        enumerable: true,
        writable: true,
        value: Worker,
    });
} else {
    let onerror = null;
    let onmessage = null;
    let onmessageerror = null;
    // Make the global an EventTarget
    let eventTarget = new EventTarget();

    setReceiver((...args) =>
        messageReceiver(globalThis, ...args));

    const {addEventListener, removeEventListener, dispatchEvent} = eventTarget;

    Object.assign(globalThis, {
        addEventListener: addEventListener.bind(eventTarget),
        removeEventListener: removeEventListener.bind(eventTarget),
        dispatchEvent: dispatchEvent.bind(eventTarget),
    });

    Object.defineProperties(globalThis, {
        name: {
            get() {
                return getName();
            },
        },
        onerror: {
            set(handler) {
                if (onerror)
                    this.removeEventListener('error', onerror);
                this.addEventListener('error', onerror = handler);
            },
        },
        onmessage: {
            set(handler) {
                if (onmessage)
                    this.removeEventListener('message', onmessage);
                this.addEventListener('message', onmessage = handler);
            },
        },
        onmessageerror: {
            set(handler) {
                if (onmessageerror)
                    this.removeEventListener('messageerror', onmessageerror);
                this.addEventListener('messageerror', onmessageerror = handler);
            },
        },
    });
}
