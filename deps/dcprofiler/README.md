# dcprofiler

`dcprofiler` is a profiling toolkit for Dreamcast applications that uses GCC's `-finstrument-functions` feature to track function calls. It records detailed timing and performance counter data, generating a call graph visualization that highlights hotspots in your code. `dcprofiler` generates a lot of tracing data — use **dcload-ip for best performance**.

There are two main parts to this project:
1. `profiler.c` – included in your Dreamcast project
2. `dctrace.py` – a Python script that parses `trace.bin` and generates a Graphviz `.dot` file

---

## Setup Instructions

1. **Add** `profiler.c` to your project’s source directory.
2. **Modify your Makefile**:
   - Add `-g -finstrument-functions` to `KOS_CFLAGS`
   - Add `profiler.o` to `OBJS`
3. **Upload your ELF** to the Dreamcast via dc-tool-ip:
   ```sh
   sudo /path/to/dc-tool-ip -c "." -t 192.168.1.137 -x program.elf
   ```
4. **Run your Dreamcast app** - profiling begins immediately and stops on exit. The results are saved to trace.bin on your PC.
5. **Generate the call graph by running**:
   ```sh
   python3 dctrace.py [OPTIONS] program.elf
   ```
> **Note**: Make sure the following are true before running `dctrace.py`:
> - `KOS_ADDR2LINE` is set **or** `sh-elf-addr2line` is located at `/opt/toolchains/dc/sh-elf/bin/`
> - Your `trace.bin` and the corresponding `.elf` file are in the same directory

6.  **Render the graph with Graphviz**:
  ```sh
  dot -Tpng graph.dot -o graph.png
  ```

### Installing Graphviz

To generate images from `.dot` files, you'll need to install the **Graphviz** tool.

- Download it from the official site: [https://graphviz.org/download](https://graphviz.org/download)

- On macOS (with Homebrew), run:
  ```sh
  brew install graphviz
  ```

## dctrace.py Optional Flags

The `dctrace.py` script accepts several options to customize the profiling output:

| Option              | Description                                                                                         |
|---------------------|-----------------------------------------------------------------------------------------------------|
| `-t <filename>`     | Specify the input trace file (default: `trace.bin`).                                                |
| `-a <path>`         | Path to the `sh-elf-addr2line` tool (default: `/opt/toolchains/dc/sh-elf/bin/sh-elf-addr2line`).    |
| `-p <percent>`      | Percentage threshold for graph visibility. Functions below this inclusive time % are omitted.       |
| `-ev0="<label>"`    | Custom label to display for event counter 0 (default: `ev0`).                                       |
| `-ev1="<label>"`    | Custom label to display for event counter 1 (default: `ev1`).                                       |
| `--xt <float>`      | Exclude-time threshold. Suggest excluding functions below this % of runtime (alias: `--ex-time`).   |
| `--xe <float>`      | Exclude-event threshold. Suggest excluding functions below this % of ev0/ev1 (alias: `--ex-ev`).    |
| `-v`                | Enable verbose output for debugging unmatched functions, parsing info, etc.                         |

### Example Usage

```sh
python3 dctrace.py -t my_trace.bin -p 1.5 -ev0="dcache miss" -ev1="icache miss" program.elf
```

## Example Makefile Snippet:

```make
TARGET = main.elf
DATETIME := $(shell date '+%Y-%m-%d_%I-%M-%S_%p')

profileip: $(TARGET)
	sudo /PATH/TO/dc-tool-ip -c "." -t 192.168.1.137 -x $(TARGET)

dot: trace.bin $(TARGET)
	python3 /PATH/TO/dctrace.py $(TARGET)

image: dot
	dot -Tpng graph.dot -o graph_$(DATETIME).png
```
**After profiling your app with make profileip, simply run**:
```sh
make image
```
This generates graph.dot and the visual graph.png.

## TIPS:

### Excluding Functions from Profiling

To prevent specific functions from being instrumented:

- Add the following attribute to their **declaration or definition**:
  ```c
  __attribute__ ((no_instrument_function))
  ```
- Or use this in your Makefile to exclude by name:
  ```make
  -finstrument-functions-exclude-function-list=func1,func2,...
  ```

---

### Excluding Entire Files

To exclude whole files from instrumentation, use:
  ```make
  -finstrument-functions-exclude-file-list=file1.c,file2.c
  ```
---

### Inlining & Optimization

Functions that are **inlined** by the compiler are not instrumented and won’t appear in traces.

Additionally, high optimization levels (like -O2 or -O3) may:
- Remove/merge function prologues and epilogues
- Cause missing or inaccurate __cyg_profile_func_* callbacks

> **Solution:**  
> Disable inlining using `-fno-inline`

---

### Tail Call Optimizations (TCO)

TCO can replace a `call + return` sequence with a direct `jump`, which **bypasses** `__cyg_profile_func_exit`.

> **Solution:**  
> Disable tail-call optimizations using `-fno-optimize-sibling-calls`
