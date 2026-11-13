"""(butterfly) The hot-function list and the code order for the next build, from a profiling run's samples.

    python -I nx2/hot_from_samples.py <samples.txt> <longshot.map> <build files folder> [--max <n>] [--share <0..1>]

samples.txt is HOS_HOT_SAMPLE's file (host/mkw_glue.c): "rva0 rva1 rva2 count" a line, the starts of the three innermost functions of
the exe on a sampled thread's stack. longshot.map is that same build's map (make_game.py keeps it in the build files folder). A
sample counts for the first lifted function (f_<guest address>) among its three: the time in a runtime helper it called (a64_*,
hos_*) is its caller's. Written into the build files folder:
  hot_funcs.txt  the hottest lifted functions, one a line, until they hold --share of the samples (0.85) or --max of them (1200):
                 the build compiles them together at -O2 with the float helpers inline (A64_HOT_FUNCS, build.py's code_hot.c)
  hot_order.txt  every lifted function the samples met, hottest first: the linker packs them together (build.py HOT_ORDER)
A run with a different exe than the map's names nothing right: the map is replaced by every build, so profile right after one.
Only names and counts are read and written, no game code.
"""
import bisect
import collections
import os
import re
import sys

ROW = re.compile(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8,16})\b")
BASE = re.compile(r"Preferred load address is ([0-9a-fA-F]+)")


def read_map(path):
    base = 0x140000000
    rows = []
    with open(path, errors="replace") as f:
        for line in f:
            m = BASE.search(line)
            if m:
                base = int(m.group(1), 16)
                continue
            m = ROW.match(line)
            if m:
                rows.append((int(m.group(2), 16), m.group(1)))
    rows.sort()
    return base, [r[0] for r in rows], [r[1] for r in rows]


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    opts = {argv[i]: argv[i + 1] for i in range(len(argv) - 1) if argv[i].startswith("--")}
    for a in opts.values():
        args.remove(a)
    if len(args) != 3:
        print(__doc__)
        return 2
    samples, map_path, work = args
    most = int(opts.get("--max", 1200))
    share = float(opts.get("--share", 0.85))
    if not os.path.isfile(map_path):
        print("hot_from_samples: no map at %s: build once with this version of Butterfly first" % map_path)
        return 1
    base, addrs, names = read_map(map_path)
    if not addrs:
        print("hot_from_samples: %s names no functions" % map_path)
        return 1
    cache = {}

    def name(rva):
        if not rva:
            return None
        n = cache.get(rva)
        if n is None:
            i = bisect.bisect_right(addrs, base + rva) - 1
            n = cache[rva] = names[i] if i >= 0 else "?"
        return n

    self_n = collections.Counter()
    incl = collections.Counter()
    other = collections.Counter()   # (samples with no lifted function in their three: the runtime's or the host's own)
    total = 0
    with open(samples) as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 4:
                continue
            fr = [int(x, 16) for x in parts[:3]]
            n = int(parts[3])
            total += n
            ns = [name(r) for r in fr if r]
            lifted = [x for x in ns if x and x.startswith("f_")]
            if lifted:
                self_n[lifted[0]] += n
                for x in set(lifted):
                    incl[x] += n
            elif ns:
                other[ns[0]] += n
    guest = sum(self_n.values())
    if not guest:
        print("hot_from_samples: %d samples, none in the game's code: is the map this build's?" % total)
        return 1
    hot, held = [], 0
    floor = max(3, int(guest * 0.0001))
    for fn, n in self_n.most_common():
        if len(hot) >= most or held >= guest * share or n < floor:
            break
        hot.append(fn)
        held += n
    order = sorted(incl, key=lambda x: (-(self_n[x] + incl[x]), x))
    with open(os.path.join(work, "hot_funcs.txt"), "w", newline="\n") as f:
        f.write("\n".join(hot) + "\n")
    with open(os.path.join(work, "hot_order.txt"), "w", newline="\n") as f:
        f.write("\n".join(order) + "\n")
    print("hot_from_samples: %d samples, %.0f%% of them in the game's code (%d functions met)" % (total, 100.0 * guest / total, len(self_n)))
    print("  hot_funcs.txt: %d functions, %.0f%% of the game's code time" % (len(hot), 100.0 * held / guest))
    print("  hot_order.txt: %d functions" % len(order))
    print("  the hottest:", ", ".join("%s %.1f%%" % (fn, 100.0 * n / guest) for fn, n in self_n.most_common(8)))
    if other:
        print("  outside the game's code:", ", ".join("%s %.1f%%" % (fn, 100.0 * n / total) for fn, n in other.most_common(6)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
