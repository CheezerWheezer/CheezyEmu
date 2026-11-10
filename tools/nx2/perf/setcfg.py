"""setcfg.py name=value ... : sets per-game settings in the TEST instance's Ocarina config
(clears the 'default' flag so the value is used)."""
import os, re, sys
c = os.path.join(os.environ.get("CHEEZYEMU_TEST_BIN") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "build", "dev2", "bin"), "user", "config", "custom", "0400486024DEC000.ini")
t = open(c, encoding='utf-8', newline='').read()
for pair in sys.argv[1:]:
    name, _, value = pair.partition('=')
    for key, val in ((name + r'\use_global', 'false'), (name + r'\default', 'false'), (name, value)):
        pat = re.compile(r'^' + re.escape(key) + r'=.*?(\r?)$', re.M)
        assert pat.search(t), key
        t = pat.sub(lambda m: key + '=' + val + m.group(1), t)
    print('set', name, '=', value)
open(c, 'w', encoding='utf-8', newline='').write(t)
