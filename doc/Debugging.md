# Debugging

Internal notes about the DAP implementation

## Development

```sh
# Build GJS
ninja -C _build

# Enter the environment
meson devenv -C _build/ --workdir .

# Run the debug adapter
./tools/dap-test.py | _build/gjs-console --inspect test.js

```
