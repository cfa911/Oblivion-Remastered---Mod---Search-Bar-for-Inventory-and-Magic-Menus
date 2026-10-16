"""Adds the sections and keys (with their comment lines) that the template has and the live ini lacks. Values already in the live ini are never changed.
Usage: merge_ini.py <template.ini> <live.ini>"""
import sys

template_path, live_path = sys.argv[1], sys.argv[2]
LF = chr(10)
CR = chr(13)


def read(p):
    raw = open(p, encoding='utf-8', newline='').read()
    crlf = CR + LF in raw
    return raw.replace(CR + LF, LF).split(LF), crlf


def parse(lines):
    """-> ordered list of [section, [lines]]; text before the first section has the name ''"""
    out = [['', []]]
    for ln in lines:
        st = ln.strip()
        if st.startswith('[') and st.endswith(']'):
            out.append([st[1:-1], [ln]])
        else:
            out[-1][1].append(ln)
    return out


def key_of(ln):
    st = ln.strip()
    if not st or st.startswith(';') or st.startswith('#') or '=' not in st:
        return None
    return st.split('=', 1)[0].strip().lower()


def entries(body):
    """a section's lines grouped into (comment lines + key line) entries -> list of (key or None, [lines])"""
    out, pending = [], []
    for ln in body:
        k = key_of(ln)
        if k is None:
            pending.append(ln)
        else:
            out.append((k, pending + [ln]))
            pending = []
    if pending:
        out.append((None, pending))
    return out


tmpl_lines, _ = read(template_path)
live_lines, live_crlf = read(live_path)
tmpl = parse(tmpl_lines)
live = parse(live_lines)
live_by_name = {name.lower(): body for name, body in live if name}
added_sections, added_keys = [], []

for name, body in tmpl:
    if not name:
        continue
    if name.lower() not in live_by_name:
        # a new section: its lines as the template has them
        while live[-1][1] and live[-1][1][-1].strip() == '':
            live[-1][1].pop()
        live.append([name, [''] + body])
        live_by_name[name.lower()] = live[-1][1]
        added_sections.append(name)
        continue
    have = {key_of(ln) for ln in live_by_name[name.lower()] if key_of(ln)}
    missing = [(k, ls) for k, ls in entries(body[1:]) if k and k not in have]
    if missing:
        target = live_by_name[name.lower()]
        while target and target[-1].strip() == '':
            target.pop()
        for k, ls in missing:
            target.extend(ls)
            added_keys.append('%s.%s' % (name, k))
        target.append('')

out = []
for name, body in live:
    out.extend(body)
text = LF.join(out)
if not text.endswith(LF):
    text += LF
if live_crlf:
    text = text.replace(LF, CR + LF)
open(live_path, 'w', encoding='utf-8', newline='').write(text)
print('ini merged: sections added %s, keys added %s' % (added_sections or 'none', added_keys or 'none'))
