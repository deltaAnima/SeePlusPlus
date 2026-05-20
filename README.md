# SeePlusPlus
👁️++

## Building

> [!IMPORTANT]
> Initialise submodules before building — the dependencies are not downloaded automatically.
> ```bash
> git submodule update --init --recursive
> ```

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```
