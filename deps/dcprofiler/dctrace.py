#!/usr/bin/env python3
"""
dctrace.py – Python utility for decoding compressed Dreamcast profiler output,
and generating a visual call graph with detailed performance stats.

This script works in conjunction with a Dreamcast profiler that:
  - Uses -finstrument-functions to log function entry/exit
  - Delta-encodes performance counter and timestamp values
  - Writes records in a compact binary format (trace.bin)

Each record is variable-length (typically 9–19 bytes), consisting of:
  - uint32_t address:
      - Bits 31     = 1 for entry, 0 for exit
      - Bits 30–22  = thread ID
      - Bits 21–0   = compressed function address (>>2 from 0x8C000000)
  - LEB128 encoded:
      - uint32_t scaled_time (delta time / 80ns)
      - uint32_t delta_evt0 (e.g. operand cache misses)
      - uint32_t delta_evt1 (e.g. instruction cache misses)

This script performs:
  ✓ LEB128 decoding of all deltas
  ✓ Symbol resolution using addr2line
  ✓ Accurate wall-clock runtime reconstruction
  ✓ Call graph reconstruction and self/inclusive time breakdown
  ✓ Parent → child contribution tracking
  ✓ Low-impact function detection and Makefile CFLAGS suggestions
  ✓ DOT file generation for Graphviz

Output:
  - graph.dot: a visual call graph (render using `dot -Tpng graph.dot -o graph.png`)
  - Console: list of functions you may wish to exclude from profiling for smaller traces

Example usage:
    python3 dctrace.py -t trace.bin -p 2 myprogram.elf
    dot -Tsvg graph.dot -o graph.svg
"""
import argparse
import struct
import subprocess
import sys
import time
import os
import traceback
from bisect import insort
from collections import defaultdict

# Constants matching the C version
ENTRY_FLAG      = 0x80000000
TID_MASK        = 0x1FF
ADDR_MASK       = 0x003FFFFF
BASE_ADDRESS    = 0x8C000000

DEFAULT_TRACE   = 'trace.bin'
DEFAULT_ADDR2LINE = os.environ.get('KOS_ADDR2LINE', '/opt/toolchains/dc/sh-elf/bin/sh-elf-addr2line')
PQ_MAX_SIZE     = 5

# --- Data structures ---
class StackFrame:
    __slots__ = ('addr','start_time','start_e0','start_e1')
    def __init__(self, addr, start_time, start_e0, start_e1):
        self.addr = addr
        self.start_time = start_time
        self.start_e0 = start_e0
        self.start_e1 = start_e1

class FunctionRecord:
    __slots__ = ('name','total_time','times_called','ev0','ev1')
    def __init__(self, name):
        self.name = name
        self.total_time = 0
        self.times_called = 0
        self.ev0 = 0
        self.ev1 = 0

class ChildCall:
    __slots__ = ('total_cycles','times_called','ev0','ev1')
    def __init__(self):
        self.total_cycles = 0
        self.times_called = 0
        self.ev0 = 0
        self.ev1 = 0

class PriorityQueue:
    def __init__(self, max_size=PQ_MAX_SIZE):
        self.max_size = max_size
        self.elements = []  # list of tuples (-percentage, from_idx, cycles)

    def insert(self, from_idx, percentage, cycles):
        # If full and new is no better than worst, skip
        if len(self.elements) == self.max_size and percentage <= -self.elements[-1][0]:
            return
        # Insert and keep sorted descending by percentage
        insort(self.elements, (-percentage, from_idx, cycles))
        # Trim to max_size
        if len(self.elements) > self.max_size:
            self.elements.pop()

    @property
    def size(self):
        return len(self.elements)

# --- Global mappings ---
functions = {}  # addr -> FunctionRecord
child_calls = defaultdict(lambda: defaultdict(ChildCall))  # parent_addr -> (child_addr -> ChildCall)
call_stack = []  # list of StackFrame

class DotManager:
    def __init__(self, program, addr2line_path, verbose, threshold, total, ev0, ev1):
        self.program     = program
        self.addr2line     = addr2line_path
        self.verbose       = verbose
        self.threshold     = threshold
        self.pq            = PriorityQueue()
        self.total         = total
        self.ev0           = ev0
        self.ev1           = ev1

    def color_from_percent(self, pct):
        wl = 440 + pct*(220/100)
        if wl < 490:
            r, g, b = 0, (wl-440)/(490-440), 1
        elif wl < 510:
            r, g, b = 0,1,-(wl-510)/(510-490)
        elif wl < 580:
            r, g, b = (wl-510)/(580-510),1,0
        elif wl < 645:
            r, g, b = 1, -(wl-645)/(645-580),0
        else:
            r, g, b = 1,0,0
        r,g,b = [int(x*255*0.7) for x in (r,g,b)]
        return f"#{r:02x}{g:02x}{b:02x}"

    def format_float_smart(self, val):
        return f"{val:.0f}" if val == int(val) else f"{val:.2f}"

    def write_node_shapes(self, fp):
        for addr, fn in functions.items():
            if fn.times_called == 0:
                continue
            cum = fn.total_time
            # sum cycles spent in children (excluding self-calls)
            child_cycles = sum(cc.total_cycles
                               for callee, cc in child_calls.get(addr, {}).items()
                               if callee != addr)
            inclusive = cum - child_cycles
            pct_cum = (cum / self.total) * 100
            pct_inclusive = (inclusive / self.total) * 100
            if pct_inclusive < self.threshold:
                continue

            # insert into priority queue
            self.pq.insert(addr, pct_inclusive, cum)

            # if recursive, record full cumulative into that child edge
            if addr in child_calls.get(addr, {}):
                child_calls[addr][addr].total_cycles = cum

            col   = self.color_from_percent(pct_cum)
            shape = "rectangle" if child_cycles else "ellipse"
            label = f"{fn.name}\\n{pct_cum:.2f}%\\n"
            children = child_calls.get(addr, {})

            # Only include inclusive % line if it's NOT a leaf
            if child_cycles:
                label += f"({pct_inclusive:.2f}%)\\n"

            # Avoid division by zero just in case
            ev0_avg = fn.ev0 / fn.times_called if fn.times_called else 0
            ev1_avg = fn.ev1 / fn.times_called if fn.times_called else 0

            label += f"{self.ev0}: {self.format_float_smart(ev0_avg)} / call\\n"

            if child_cycles:
                child_ev0_total = sum(cc.ev0 for callee, cc in children.items() if callee != addr)
                inclusive_ev0 = fn.ev0 - child_ev0_total
                inclusive_ev0_avg = inclusive_ev0 / fn.times_called if fn.times_called else 0
                label += f"({self.ev0}: {self.format_float_smart(inclusive_ev0_avg)} / call)\\n"

            label += f"{self.ev1}: {self.format_float_smart(ev1_avg)} / call\\n"

            if child_cycles:
                child_ev1_total = sum(cc.ev1 for callee, cc in children.items() if callee != addr)
                inclusive_ev1 = fn.ev1 - child_ev1_total
                inclusive_ev1_avg = inclusive_ev1 / fn.times_called if fn.times_called else 0
                label += f"({self.ev1}: {self.format_float_smart(inclusive_ev1_avg)} / call)\\n"

            label += f"{fn.times_called} x"

            fp.write(
                f"\t\t\"{fn.name}\" [label=\"{label}\" "
                f"fontcolor=\"white\" color=\"{col}\" shape={shape}]\n"
            )
        fp.write("\n")

    def write_call_graph(self, fp):
        for caller, callee_dict in child_calls.items():
            for callee, cc in callee_dict.items():
                if cc.times_called == 0:
                    continue
                pct = (cc.total_cycles / self.total) * 100
                if pct < self.threshold:
                    continue
                col   = self.color_from_percent(pct)
                style = "bold" if pct > 0.35 else "solid"
                name_f = functions[caller].name
                name_t = functions[callee].name
                label = f"{pct:.2f}%\\n{cc.times_called} x"
                fp.write(
                    f"\t\t\"{name_f}\" -> \"{name_t}\" "
                    f"[label=\"{label}\" color=\"{col}\" style=\"{style}\" fontsize=10]\n"
                )

    def write_table(self, fp):
        elements = self.pq.elements
        last_index = len(elements)

        fp.write("\t\ta0 [shape=none label=<\n\t\t\t<TABLE border=\"0\" cellspacing=\"3\" cellpadding=\"10\" bgcolor=\"black\">\n\t\t\t\t")

        for rank, (neg_pct, idx, cycles) in enumerate(elements, 1):
            pct = -neg_pct
            name = functions[idx].name
            fp.write("<TR>\n\t\t\t\t\t")
            fp.write(f"<TD bgcolor=\"white\">{rank}</TD>\n\t\t\t\t\t")
            fp.write(f"<TD bgcolor=\"white\">{name}</TD>\n\t\t\t\t\t")
            fp.write(f"<TD bgcolor=\"white\">{pct:.2f}%</TD>\n\t\t\t\t\t")
            fp.write("</TR>")

            if rank != last_index:
                fp.write("\n\n\t\t\t\t")  # Extra spacing *only* between rows

        fp.write("\n\t\t\t</TABLE>\n\t\t>];\n")

    def write_graph_caption(self, fp):
        now = time.localtime()
        ampm = 'PM' if now.tm_hour >= 12 else 'AM'
        hour = now.tm_hour % 12 or 12
        secs = (self.total) / 1e9
        label = (f"{self.program}\n\t\tRuntime: {secs:.3f} secs\n\t\t"
                 f"{now.tm_mon+1}/{now.tm_mday}/{now.tm_year} @ "
                 f"{hour}:{now.tm_min:02d} {ampm}")

        fp.write("\n\tgraph [\n"
                 "\t\tfontname = \"Helvetica-Oblique\",\n"
                 "\t\tfontsize = 32,\n")
        fp.write(f"\t\tlabel=\"{label}\"\n\t];")

    def create_dot_file(self):
        with open('graph.dot', 'w') as fp:
            fp.write("digraph program {\n\n\t")

            # Write the graph cluster
            fp.write("subgraph cluster0 {\n\t\t"
                     "ratio=fill;\n\t\t"
                     "node [style=filled];\n\t\t"
                     "peripheries=0;\n\n")

            self.write_node_shapes(fp)
            self.write_call_graph(fp)
            fp.write("\t}\n\n\t")

            # Write the table cluster
            fp.write("subgraph cluster1 {\n\t\t"
                     "peripheries=0;\n\t\t"
                     "node [fontname=\"Helvetica,Arial,sans-serif\" fontsize=22]\n\t\t"
                     "edge [fontname=\"Helvetica,Arial,sans-serif\" fontsize=22]\n\n")
            self.write_table(fp)
            fp.write("\t}\n")

            self.write_graph_caption(fp)
            fp.write("\n}\n")

# ----------------------------------------------------------------------------
# main: parse trace.bin and drive analysis
# ----------------------------------------------------------------------------
def addr2name(addr, addr2line, program):
    try:
        p = subprocess.Popen([addr2line, '-e', program, '-f', '-s', hex(addr)],
                             stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL)
        name = p.stdout.readline().strip().decode()
        return name or hex(addr)
    except Exception:
        return hex(addr)

def print_progress_bar(progress, bar_length=50):
    """
    Prints a progress bar to the console.

    :param progress: Progress percentage (0–100)
    :param bar_length: Length of the bar in characters
    """
    filled_length = int(bar_length * progress // 100)
    bar = '#' * filled_length + '-' * (bar_length - filled_length)
    sys.stdout.write(f'\r[{bar}] {progress}%')
    sys.stdout.flush()

    if progress == 100:
        print()

def usage():
    print("Usage: python3 dctrace.py [OPTIONS] <program.elf>")
    print("Requires: A trace.bin, sh-elf-addr2line\n")
    print("OPTIONS:")
    print("  -t <filename>     Set trace file to <filename> (default: trace.bin)")
    print("  -a <filepath>     Set sh-elf-addr2line filepath to <filepath>")
    print("                    (default: /opt/toolchains/dc/sh-elf/bin/sh-elf-addr2line)")
    print("  -p <percentage>   Set percentage threshold. Every function under this threshold")
    print("                    will not show up in the dot file (default: 0; 0–100 range)")
    print("  -ev0=\"<label>\"  Custom label for ev0 (default: ev0)")
    print("  -ev1=\"<label>\"  Custom label for ev1 (default: ev1)")
    print("  --xt <float>      Suggest exclude for functions below this % of runtime")
    print("                         (alias: --ex-time, default: 3.0)")
    print("  --xe <float>      Suggest exclude for functions using less than this % of")
    print("                         ev0 and ev1 (alias: --ex-ev, default: 1.0)")
    print("  -v                Verbose output")
    sys.exit(1)

def parse_args():
    p = argparse.ArgumentParser(description="dctrace")
    p.add_argument('-t', '--trace', default=DEFAULT_TRACE)
    p.add_argument('-a', '--addr2line', default=DEFAULT_ADDR2LINE)
    p.add_argument('-p', '--percentage', type=float, default=0)
    p.add_argument('-v', '--verbose', action='store_true')
    p.add_argument('-ev0', '--ev0-label', default='ev0', help='Custom label for ev0 (default: ev0)')
    p.add_argument('-ev1', '--ev1-label', default='ev1', help='Custom label for ev1 (default: ev1)')
    p.add_argument('--xt', '--ex-time', dest='exclude_time_threshold', type=float, default=3.0,
               help='Suggest exclude for functions below this % of runtime (default: 3.0)')
    p.add_argument('--xe', '--ex-ev', dest='exclude_ev_threshold', type=float, default=1.0,
               help='Suggest exclude for functions below this % of ev0/ev1 usage (default: 1.0)')
    p.add_argument('program', help='path to ELF executable')
    return p.parse_args()

def suggest_exclude_functions(total_time, total_ev0, total_ev1, percent_threshold, ev_percent_threshold):
    """
    Suggests low-impact functions to exclude from instrumentation.

    Criteria for exclusion:
    - Inclusive time < `percent_threshold` (% of total runtime).
    - Inclusive ev0 and ev1 < `ev_percent_threshold` (% of total ev0/ev1 counts).
    - Called at least once (times_called > 0).

    This version includes functions with children, as long as the total impact remains small.
    That way, even small utility functions that call other tiny helpers can be excluded.

    This helps reduce trace size and overhead on the Dreamcast.
    """
    candidates = []

    for addr, fn in functions.items():
        if fn.times_called == 0:
            continue

        name = fn.name
        inclusive_time_pct = (fn.total_time / total_time) * 100 if total_time else 0
        ev0_pct = (fn.ev0 / total_ev0) * 100 if total_ev0 else 0
        ev1_pct = (fn.ev1 / total_ev1) * 100 if total_ev1 else 0

        if (
            inclusive_time_pct < percent_threshold and
            ev0_pct < ev_percent_threshold and
            ev1_pct < ev_percent_threshold
        ):
            candidates.append((inclusive_time_pct, name))

    if not candidates:
        return

    candidates.sort()
    exclude_names = [name for _, name in candidates]
    total_pct = sum(p for p, _ in candidates)

    print("\n Suggested low-impact functions to exclude from instrumentation:")
    print(f"    (Collectively account for {total_pct:.2f}% of total runtime)\n")
    
    # Multiline Makefile-friendly format
    print("EXCLUDE_FUNCS = \\")
    for name in exclude_names:
        print(f"    {name} \\")

    print("CFLAGS += -finstrument-functions-exclude-function-list=$(EXCLUDE_FUNCS)\n")
    print("  NOTE: Be sure your CFLAGS appear *before* kos-cc or kos-c++ in your Makefile command:")
    print("    Example:")
    print("    $(TARGET): $(OBJS)")
    print("        kos-cc $(CFLAGS) -o $(TARGET)\n")

def read_uleb128(f):
    result = 0
    shift = 0
    count = 0
    while True:
        byte = f.read(1)
        if not byte:
            raise EOFError("Unexpected EOF while reading ULEB128")
        b = byte[0]
        result |= (b & 0x7F) << shift
        count += 1
        if not (b & 0x80):
            break
        shift += 7
    return result, count

def main():
    args = parse_args()

    try:
        with open(args.trace, 'rb') as f:
            current_time = 0
            current_e0 = 0
            current_e1 = 0

            total_size = os.path.getsize(args.trace)
            read_size = 0

            total_size = os.path.getsize(args.trace)
            read_size = 0

            while True:
                header = f.read(4)
                if not header or len(header) < 4:
                    break

                entry_tid_addr = struct.unpack('<I', header)[0]
                read_size += 4

                try:
                    delta_time, t_bytes = read_uleb128(f)
                    delta_evt0, e0_bytes = read_uleb128(f)
                    delta_evt1, e1_bytes = read_uleb128(f)
                    read_size += t_bytes + e0_bytes + e1_bytes
                except EOFError:
                    if args.verbose:
                        print("Incomplete record at end of file; skipping.")
                    break

                progress = int((read_size / total_size) * 100)
                print_progress_bar(progress)

                is_entry = bool((entry_tid_addr >> 31) & 1)
                thread_id = (entry_tid_addr >> 22) & TID_MASK
                compressed_addr = entry_tid_addr & ADDR_MASK
                address = (compressed_addr << 2) + BASE_ADDRESS

                current_time += delta_time * 80
                current_e0 += delta_evt0
                current_e1 += delta_evt1

                if is_entry:
                    # -- ENTRY logic --
                    if address not in functions:
                        func_name = addr2name(address, args.addr2line, args.program)
                        functions[address] = FunctionRecord(func_name)
                    func = functions[address]
                    func.times_called += 1

                    # record child call count
                    if call_stack:
                        parent = call_stack[-1].addr
                        cc = child_calls[parent][address]
                        cc.times_called += 1

                    # push new frame
                    frame = StackFrame(address, current_time, current_e0, current_e1)
                    call_stack.append(frame)

                else:
                    # -- EXIT logic --
                    if not call_stack:
                        # unmatched exit, skip
                        continue

                    frame = call_stack.pop()
                    delta_time = current_time - frame.start_time
                    delta_e0 = current_e0 - frame.start_e0
                    delta_e1 = current_e1 - frame.start_e1

                    # update function totals
                    func = functions[frame.addr]
                    func.total_time += delta_time
                    func.ev0 += delta_e0
                    func.ev1 += delta_e1

                    # update child call accumulation
                    if call_stack:
                        parent = call_stack[-1]
                        cc = child_calls[parent.addr][frame.addr]
                        cc.total_cycles += delta_time
                        cc.ev0 += delta_e0
                        cc.ev1 += delta_e1

            # Final cleanup for any unmatched function entries
            if call_stack:
                if args.verbose:
                    print(f"Warning: {len(call_stack)} unmatched function entries detected. Processing them as incomplete frames.")

                for i, frame in enumerate(call_stack):
                    delta_time = current_time - frame.start_time
                    delta_e0 = current_e0 - frame.start_e0
                    delta_e1 = current_e1 - frame.start_e1

                    func = functions[frame.addr]
                    if args.verbose:
                        print(f"  Function: {func.name} at depth {i}")
                    func.total_time += delta_time
                    func.ev0 += delta_e0
                    func.ev1 += delta_e1

                    if i > 0:
                        parent = call_stack[i - 1].addr
                        cc = child_calls[parent][frame.addr]
                        cc.total_cycles += delta_time
                        cc.ev0 += delta_e0
                        cc.ev1 += delta_e1

            # Suggest some functions the user can remove from intrumenstation after the first run
            suggest_exclude_functions(current_time, current_e0, current_e1, args.exclude_time_threshold, args.exclude_ev_threshold)

            dm = DotManager(args.program, args.addr2line, args.verbose, args.percentage, current_time, args.ev0_label, args.ev1_label)
            dm.create_dot_file()
    except FileNotFoundError:
        print(f"Error: file '{args.infile}' not found.\n\n")
        usage()
    except Exception as e:
        print("An error occurred:")
        traceback.print_exc()   # prints file, line, and stack trace
        print("\n\n")
        usage()

if __name__ == '__main__':
    main()
