/* eslint-disable spaced-comment */
/// <reference lib="dom" />

// onmessage = event => {
//     console.log(`Why hello there main thread, I got ${event.data} from you. I'm adding 10.`);
//     postMessage(event.data + 10);
// };

let i = 0;
const id = setInterval(() => {
    i++;
    console.log(`t: ${i}`);
    if (i > 5) {
        clearInterval(id);
        postMessage(i);
    }
}, 1000);
