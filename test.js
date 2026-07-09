function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

for (let i = 0; i < 60; i++) {
    await sleep(1_000);
    console.log(`Iteration ${i}`);
}
