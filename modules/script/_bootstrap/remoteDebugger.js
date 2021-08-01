/* global debuggee, loadNative, writeMessage, startRemoteDebugging,  */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>
// @ts-check

// @ts-expect-error
const {print} = loadNative('_print');
/** @type {(msg: string) => void} */
const debug = print;

function connectionPrefix(connectionId) {
    return ['gjs', `conn${connectionId}`];
}

/** @type {Map<string, number>} */
const actorCount = new Map();
const connectionPool = new Map();

/**
 * Makes an identifier unique by appending a counter to it
 *
 * @param {string} baseIdentifier the identifier that needs to be made unique
 * @returns {number}
 */
function getActorCount(baseIdentifier) {
    const count = actorCount.get(baseIdentifier) ?? 1;

    actorCount.set(baseIdentifier, count + 1);

    return count;
}

/**
 *
 * @param {number} connectionId the connection id for this actor
 * @param {string} actorType the actor type name for this identifier
 */
function createId(connectionId, actorType) {
    const baseIdentifier = [...connectionPrefix(connectionId), actorType].join(
        '.'
    );

    return `${baseIdentifier}${getActorCount(baseIdentifier)}`;
}

/** @type {Map<string, Actor>} */
const actorMap = new Map();

/**
 * @param {Actor} actor an actor to register by ID
 */
function registerActor(actor) {
    actorMap.set(actor.actorID, actor);
}

class Actor {
    /**
     * @param {number} connectionId the connection id for this actor
     * @param {string} typeName the type name for this actor
     * @param {string} actorID the unique identifier for this actor
     */
    constructor(connectionId, typeName, actorID) {
        this.connectionId = connectionId;
        this.typeName = typeName;
        this.actorID = actorID;

        registerActor(this);
    }

    form() {
        return {};
    }

    write(json) {
        const bytes = JSON.stringify(json);
        debug(bytes);
        writeMessage(this.connectionId, `${bytes.length}:${bytes}`);
    }
}

class GlobalActor extends Actor {
    /**
     * @param {number} connectionId the connection id for this actor
     * @param {string} typeName the type name for this actor
     */
    constructor(connectionId, typeName) {
        super(
            connectionId,
            typeName,
            createId(connectionId, `${typeName}Actor`)
        );
    }
}

class TargetActor extends Actor {
    /**
     * @param {number} connectionId the connection id for this actor
     * @param {string} typeName the type name for this actor
     * @param {GlobalActor} parent the parent global actor for this actor
     */
    constructor(connectionId, typeName, parent) {
        super(connectionId, typeName, childOf(parent.actorID, typeName));
    }
}

class DeviceActor extends GlobalActor {
    constructor(connectionId) {
        super(connectionId, 'device');
    }

    getDescription() {
        return {
            value: {
                // TODO(ewlsh): Create UUID
                appid: '{ec8230f7-c20a-464f-9b0e-13a3a9397381}',
                apptype: 'gjs',
                vendor: 'GNOME',
                brandName: 'gjs',
                name: 'gjs',
                // TODO(ewlsh): Figure out versioning.
                version: '89.0.2',
                platformversion: '89.0.2',
                geckoversion: '89.0.2',
                canDebugServiceWorkers: false,
            },
        };
    }
}

function childOf(parentActorId, actorType) {
    const id = [parentActorId, actorType].join('/');

    return `${id}${getActorCount(id)}`;
}

/**
 * Debugger.Source objects have a `url` property that exposes the value
 * that was passed to SpiderMonkey, but unfortunately often SpiderMonkey
 * sets a URL even in cases where it doesn't make sense, so we have to
 * explicitly ignore the URL value in these contexts to keep things a bit
 * more consistent.
 *
 * @param {Debugger.Source} source a Source object
 *
 * @returns {string | null}
 */
function getDebuggerSourceURL(source) {
    const introType = source.introductionType;

    // These are all the sources that are eval or eval-like, but may still have
    // a URL set on the source, so we explicitly ignore the source URL for these.
    if (
        introType === 'injectedScript' ||
        introType === 'eval' ||
        introType === 'debugger eval' ||
        introType === 'Function' ||
        introType === 'javascriptURL' ||
        introType === 'eventHandler' ||
        introType === 'domTimer'
    )
        return null;

    // if (source.url && !source.url.includes(":"))
    //     return `resource:///unknown/${source.url}`;

    return source.url;
}

class SourceActor extends TargetActor {
    /**
     * @param {number} connectionId the connection id for this actor
     * @param {ProcessDescriptorActor} processDescriptorActor the process descriptor actor this source should be registered to
     * @param {Debugger.Source} debuggerSource the Source object this actor wraps
     */
    constructor(connectionId, processDescriptorActor, debuggerSource) {
        super(connectionId, 'source', processDescriptorActor);

        this._debuggerSource = debuggerSource;
    }

    getBreakpointPositions() {
        return {
            positions: [],
        };
    }

    getBreakableLines() {
        return {
            lines: [],
        };
    }

    source() {
        return {
            source: this._debuggerSource?.source?.text ?? null,
        };
    }

    form() {
        const source = this._debuggerSource;

        let introductionType = source.introductionType;
        if (
            introductionType === 'srcScript' ||
            introductionType === 'inlineScript' ||
            introductionType === 'injectedScript'
        ) {
            // These three used to be one single type, so here we combine them all
            // so that clients don't see any change in behavior.
            introductionType = 'scriptElement';
        }

        return {
            actor: this.actorID,
            sourceMapBaseURL: null,
            extensionName: null,
            url: getDebuggerSourceURL(source),
            isBlackBoxed: false,
            introductionType,
            sourceMapURL: source.sourceMapURL,
        };
    }
}

const STATES = {
    //  Before ThreadActor.attach is called:
    DETACHED: 'detached',
    //  After the actor is destroyed:
    EXITED: 'exited',

    // States possible in between DETACHED AND EXITED:
    // Default state, when the thread isn't paused,
    RUNNING: 'running',
    // When paused on any type of breakpoint, or, when the client requested an interrupt.
    PAUSED: 'paused',
};

class ThreadActor extends TargetActor {
    /**
     * @param {number} connectionId the connection id for this actor
     * @param {ProcessDescriptorActor} processTargetActor the process target this thread's sources should be registered to
     */
    constructor(connectionId, processTargetActor) {
        super(
            connectionId,
            'thread',
            processTargetActor.contentProcessTargetActor.contentProcessActor
        );
        this.processTargetActor = processTargetActor;

        this._dbg = new Debugger();

        this._dbg.addDebuggee(debuggee);

        this._state = STATES.DETACHED;
        this._sources = [...this._dbg.findScripts()];
        this._sourceActors = this._sources.map(
            s => new SourceActor(connectionId, processTargetActor, s)
        );
    }

    get state() {
        return this._state;
    }

    attach(
        // TODO(ewlsh): Options:
        // pauseOnExceptions,
        // ignoreCaughtExceptions,
        // shouldShowOverlay,
        // shouldIncludeSavedFrames,
        // shouldIncludeAsyncLiveFrames,
        // skipBreakpoints,
        // logEventBreakpoints,
        // observeAsmJS,
        // breakpoints,
        // eventBreakpoints,
    ) {
        if (this.state === STATES.EXITED) {
            return {
                error: 'exited',
                message: 'threadActor has exited',
            };
        }

        if (this.state !== STATES.DETACHED) {
            return {
                error: 'wrongState',
                message: `Current state is ${this.state}`,
            };
        }

        this._dbg.onNewScript = this._onNewScript.bind(this);

        this._state = STATES.RUNNING;

        return {
            type: STATES.RUNNING,
            actor: this.actorID,
        };
    }

    _onNewScript(source) {
        if (this._sources.includes(source))
            return;
        const actor = new SourceActor(
            this.connectionId,
            this.processTargetActor,
            source
        );
        this._sources.push(source);
        this._sourceActors.push(actor);

        this.write({
            type: 'newSource',
            source: actor.form(),
        });
    }

    pauseOnExceptions({pauseOnExceptions, ignoreCaughtExceptions}) {
        debug(`pauseOnExceptions: ${pauseOnExceptions}`);
        debug(`ignoreCaughtExceptions: ${ignoreCaughtExceptions}`);
        return {};
    }

    sources() {
        return {
            sources: this._sourceActors.map(actor => actor.form()),
        };
    }

    // TODO(ewlsh): Handle thread reconfiguring
    reconfigure() {
        return {};
    }

    form() {
        return {
            threadActor: {
                actor: this.actorID,
            },
        };
    }
}

class ConsoleActor extends TargetActor {
    /**
     * @param {number} connectionId the connection id for this actor
     * @param {ContentProcess} contentProcessActor the content process actor this console is connected to
     */
    constructor(connectionId, contentProcessActor) {
        super(connectionId, 'console', contentProcessActor);
    }

    getCachedMessages({messageTypes}) {
        debug(JSON.stringify(messageTypes));
        return {messages: []};
    }

    startListeners({listeners}) {
        debug(JSON.stringify(listeners));
        return {listeners: []};
    }
}

class ContentProcess extends Actor {
    /**
     * @param {number} connectionId the connection id for this actor
     */
    constructor(connectionId) {
        super(
            connectionId,
            'content-process',
            createId(connectionId, 'content-process')
        );

        this.consoleActor = new ConsoleActor(connectionId, this);
    }
}

class ContentProcessTargetActor extends TargetActor {
    /**
     *
     * @param {number} connectionId the connection id for this actor
     * @param {ProcessDescriptorActor} parentActor the process descriptor this target belongs to
     */
    constructor(connectionId, parentActor) {
        const contentProcess = new ContentProcess(connectionId);

        super(connectionId, 'contentProcessTarget', contentProcess);

        this.contentProcessActor = contentProcess;
        this.parentActor = parentActor;

        this.watcherActor = new WatcherActor(
            connectionId,
            this.contentProcessActor
        );
        // this._actorID = childOf(this.actorID, "contentProcessTarget");
    }

    form() {
        return {
            processID: 0,
            actor: this.actorID,
            threadActor: this.parentActor.threadActor.actorID,
            consoleActor: this.contentProcessActor.consoleActor.actorID,
            remoteType: 'privilegedmozilla',
            traits: {
                networkMonitor: false,
                supportsTopLevelTargetFlag: false,
                noPauseOnThreadActorAttach: true,
            },
        };
    }
}

class WatcherActor extends TargetActor {
    /**
     * @param {number} connectionId the connection id for this actor
     * @param {ContentProcess} contentProcessActor the content process this watcher "watches"
     */
    constructor(connectionId, contentProcessActor) {
        super(connectionId, 'watcher', contentProcessActor);

        this.processDescriptorActor = contentProcessActor;
    }

    form() {
        return {
            actor: this.actorID,
        };
    }
}

class ProcessDescriptorActor extends GlobalActor {
    constructor(connectionId) {
        super(connectionId, 'processDescriptor');

        this.contentProcessTargetActor = new ContentProcessTargetActor(
            connectionId,
            this
        );

        // ThreadActor awkwardly depends on contentProcessTargetActor
        this.threadActor = new ThreadActor(connectionId, this);
    }

    getTarget() {
        return {
            process: this.contentProcessTargetActor.form(),
        };
    }

    getWatcher() {
        return this.contentProcessTargetActor.watcherActor.form();
    }

    form() {
        return {
            actor: this.actorID,
            id: 0,
            isParent: true,
            traits: {
                watcher: true,
                supportsReloadDescriptor: false,
            },
        };
    }
}

class PreferenceActor extends GlobalActor {
    /**
     * @param {number} connectionId the connection id for this actor
     */
    constructor(connectionId) {
        super(connectionId, 'pref');
    }

    getBoolPref() {
        return {
            value: false,
        };
    }
}

class RootActor extends Actor {
    /**
     * @param {number} connectionId the connection id for this actor
     * @param {DeviceActor} deviceActor the global device actor
     * @param {PreferenceActor} preferenceActor the global preferences actor
     * @param {ProcessDescriptorActor} mainProcessActor the global "main" process actor
     */
    constructor(connectionId, deviceActor, preferenceActor, mainProcessActor) {
        super(connectionId, 'rootActor', `root${connectionId}`);

        this.deviceActor = deviceActor;
        this.preferenceActor = preferenceActor;
        this.mainProcessActor = mainProcessActor;
    }

    getProcess(options) {
        debug(JSON.stringify(options));
        if (options.id !== 0) {
            return {
                error: 'noSuchActor!',
            };
        }

        return {
            processDescriptor: this.mainProcessActor.form(),
        };
    }

    listServiceWorkerRegistrations() {
        return {
            registrations: [],
        };
    }

    listWorkers() {
        return {
            workers: [],
        };
    }

    listTabs() {
        return {
            tabs: [],
        };
    }

    listAddons() {
        return {
            addons: [],
        };
    }

    listProcesses() {
        return {
            processes: [this.mainProcessActor.form()],
        };
    }

    getRoot() {
        return {
            deviceActor: this.deviceActor.actorID,
            preferenceActor: this.preferenceActor.actorID,
            addonsActor: null,
            heapSnapshotFileActor: null,
            perfActor: null,
            parentAccessibilityActor: null,
            screenshotActor: null,
        };
    }
}

class DebuggingConnection {
    /**
     * @param {number} connectionId the connection to wrap and register actors to
     */
    constructor(connectionId) {
        this.connectionId = connectionId;

        this.preferenceActor = new PreferenceActor(connectionId);
        this.mainProcessActor = new ProcessDescriptorActor(connectionId);
        this.deviceActor = new DeviceActor(connectionId);
        this.rootActor = new RootActor(
            connectionId,
            this.deviceActor,
            this.preferenceActor,
            this.mainProcessActor
        );

        connectionPool.set(connectionId, this);
    }
}

class RemoteDebugger {
    /**
     *
     * @param {number} receiverConnectionId the connection to write the packet to
     * @param {*} json a json packet object to send
     */
    writePacket(receiverConnectionId, json) {
        const packetString = JSON.stringify(json);

        debug(packetString);

        writeMessage(
            receiverConnectionId,
            `${packetString.length}:${packetString}`
        );
    }

    /**
     * @param {number} connectionId the connection a packet was received on
     * @param {*} json the json packet received
     * @returns {void}
     */
    onReadPacket(connectionId, json) {
        debug(JSON.stringify(json, null, 4));

        const {to, type, ...options} = json;
        let actor = actorMap.get(to);
        let from = actor?.actorID;

        // We store root actors as root1, root2, etc.
        // in our pool to uniquely identify them across
        // connections. Clients always expect 'root',
        // so we strip the unique identifier here.
        if (!actor && to === 'root') {
            actor = actorMap.get(`root${connectionId}`);
            from = 'root';
        }

        if (!actor) {
            this.writePacket(connectionId, {
                from: json.to,
                error: 'noSuchActor',
            });

            return;
        }

        if (type in actor) {
            this.writePacket(connectionId, {
                from,
                ...actor[type]?.(options) ?? null,
            });
        } else {
            this.writePacket(connectionId, {
                from,
                // TODO(ewlsh): Find the correct error code for this.
                error: 'noSuchProperty',
            });
        }
    }

    /**
     * @param {number} connectionId the connection the message was read from
     * @param {string} packetString a string of simple packets
     */
    onReadMessage(connectionId, packetString) {
        debug(packetString);

        let parsingBytes = packetString;
        try {
            const packets = [];
            while (parsingBytes.length > 0) {
                const [length, ...messageParts] = parsingBytes.split(':');
                const message = messageParts.join(':');

                const parsedLength = Number.parseInt(length, 10);
                if (Number.isNaN(parsedLength))
                    throw new Error(`Invalid length: ${length}`);

                parsingBytes = message.slice(parsedLength).trim();

                packets.push(JSON.parse(message.slice(0, parsedLength)));
            }

            packets.forEach(packet => {
                this.onReadPacket(connectionId, packet);
            });
        } catch (error) {
            debug(error);
            debug(`Failed to parse: ${packetString}`);
        }
    }

    start(port) {
        startRemoteDebugging(port);
    }

    sayHello(connection) {
        this.writePacket(connection, {
            from: 'root',
            applicationType: 'gjs',
            // TODO(ewlsh)
            testConnectionPrefix: connectionPrefix(connection).join('.'),
            traits: {},
        });
    }
}

var remoteDebugger = new RemoteDebugger();

function onMessage(connectionId, message) {
    remoteDebugger.onReadMessage(connectionId, message);
}
globalThis.onReadMessage = onMessage;

globalThis.onConnection = connectionId => {
    new DebuggingConnection(connectionId);

    remoteDebugger.sayHello(connectionId);
};

remoteDebugger.start(6080);
debug('Starting remote debugging on port 6080...');
