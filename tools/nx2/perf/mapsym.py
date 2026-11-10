"""mapsym.py <exe.map> <samples.raw> [top] : names cheezyemu.exe+0x... frames with the linker map
and prints self and inclusive time per thread and function."""
import bisect
import collections
import re
import sys

map_path, raw_path = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
BASE = 0x140000000

syms = []
in_publics = False
with open(map_path, encoding='utf-8', errors='replace') as f:
    for line in f:
        if 'Publics by Value' in line or 'Static symbols' in line:
            in_publics = True
            continue
        if not in_publics:
            continue
        parts = line.split()
        if len(parts) < 3 or ':' not in parts[0]:
            continue
        try:
            rva = int(parts[2], 16) - BASE
        except ValueError:
            continue
        if rva > 0:
            syms.append((rva, parts[1]))
syms.sort()
addrs = [s[0] for s in syms]


def short(name):
    # MSVC-mangled: ?Func@Class@Namespace@@...  -> Namespace::Class::Func
    if name.startswith('?'):
        m = re.match(r'\?+([^@]+(?:@[^@]+)*)@@', name)
        if m:
            parts = m.group(1).split('@')
            parts = [p for p in parts if p and not p.startswith('?')]
            return '::'.join(reversed(parts[:4]))[:110]
    return name[:110]


def name_of(frame):
    if frame.startswith('cheezyemu.exe+'):
        off = int(frame.split('+')[1], 16)
        i = bisect.bisect_right(addrs, off) - 1
        return short(syms[i][1]) if i >= 0 else frame
    if frame.startswith('jit+'):
        return '(jit: guest code / macro JIT)'
    return frame.split('+')[0] if '+0x' in frame and '.dll+' in frame else frame


self_t = collections.defaultdict(collections.Counter)
incl_t = collections.defaultdict(collections.Counter)
total = collections.Counter()
for line in open(raw_path, encoding='utf-8', errors='replace'):
    line = line.rstrip()
    if not line:
        continue
    stack, _, count = line.rpartition(' ')
    try:
        count = int(count)
    except ValueError:
        continue
    frames = stack.split(';')
    thread, frames = frames[0], [name_of(f) for f in frames[1:]]
    if not frames:
        continue
    total[thread] += count
    self_t[thread][frames[-1]] += count
    for fn in set(frames):
        incl_t[thread][fn] += count

for thread in total:
    n = total[thread]
    print(f'=== {thread}: {n} samples')
    print('-- self')
    for fn, c in self_t[thread].most_common(top):
        print(f'  {100 * c / n:5.1f}%  {fn}')
    print('-- inclusive')
    for fn, c in incl_t[thread].most_common(top):
        print(f'  {100 * c / n:5.1f}%  {fn}')
