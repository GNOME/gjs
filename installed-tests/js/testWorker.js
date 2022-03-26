/* eslint-disable spaced-comment */
/// <reference lib="dom" />
xdescribe('Workers', () => {
    it('runs in unblocked thread', function (done) {
        const worker = new Worker('resource:///org/gjs/jsunit/modules/worker.js', {name: '"Test Worker"'});

        worker.onmessage = event => {
            done();
        };
    });
});
