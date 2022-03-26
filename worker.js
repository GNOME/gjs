import GLib from 'gi://GLib';

onmessage = event => {
    console.log(`Why hello there main thread, I got ${event.data} from you. I'm adding 10.`);
    postMessage(event.data + 10);
};

setInterval(() => {
    console.log(`Worker ${name} is still running`);
}, 5000);

// const ml = new GLib.MainLoop(GLib.MainContext.get_thread_default(), false);
// ml.run();
