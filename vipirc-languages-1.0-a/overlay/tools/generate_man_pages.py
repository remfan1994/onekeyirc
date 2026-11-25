"""Render the canonical translated manuals as UTF-8 man(7) pages."""
from pathlib import Path
import re
import sys

def escape(line):
    return line.replace('\\', r'\e').replace('-', r'\-')

def render(source, app, version):
    lines = source.read_text().splitlines()
    headings = {m.group(1).strip() for line in lines
                if (m := re.match(r'^\s+\d+[.)]\s+(.+)', line))}
    out = [f'.TH {app.upper()} 1 "" "{app} {version}"', '.nh', '.ad l',
           '.SH NAME', app + r' \- ' + escape(next(line for line in lines if app in line and ' - ' in line).split(' - ',1)[-1]) if any(app in line and ' - ' in line for line in lines) else app,
           '.SH SYNOPSIS', '.B '+app, '[OPTION]...']
    for i,line in enumerate(lines):
        if i+1 < len(lines) and re.fullmatch(r'-{2,}',lines[i+1]):
            heading = True
        else:
            heading = line.strip() in headings and line == line.lstrip()
        if re.fullmatch(r'-{2,}',line):
            continue
        if heading:
            out.append('.SH "'+escape(line).replace('"',r'\(dq')+'"')
        elif line.strip():
            # Protect leading dots and apostrophes; keep commands and list items on their own lines.
            out.extend(['.br',r'\&'+escape(line)])
        elif out[-1] != '.PP':
            out.append('.PP')
    return '\n'.join(out)+'\n'

if __name__ == '__main__':
    app, version = sys.argv[1:]
    doc = Path('doc')
    kind = 'MINIMAL-MANUAL' if app == 'onekeyirc' else 'FULL-MANUAL'
    for lang in 'id ms tr es pt fr de ru it ar'.split():
        dest = doc/'man'/lang/(app+'.1')
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(render(doc/f'{kind}-{lang}.txt', app, version))
