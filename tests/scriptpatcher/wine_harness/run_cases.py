#!/usr/bin/env python3
"""Regression cases for the vendored Wine VBScript engine.

Each cases/*.vbs starts with a comment header:
  ' EXPECT: compile-ok | run-ok | compile-error <n> | run-error <n>
  ' WINDOWS: ok | compile-error | run-error     what Microsoft's engine (cscript) does
  ' KNOWN-BUG: <id> <text>                       optional: Wine still differs, reported XFAIL
  ' GLOBALS: Name1,Name2                         optional: host object exposing these names
  ' ITEMS: Name=Type,Name2=Type2                 optional: table elements, registered like the
                                                 device does (one named item each)

Engines:
  wine     (default) the host build of the shipped engine: --bin or $VBS_CHECK
  windows  cscript //nologo //E:vbscript, checked against the WINDOWS line
           (run it from Windows Python to re-verify the expectations); cases
           with ITEMS go through 32-bit ScriptControl, which can add named items

Results: PASS, FAIL, XFAIL (known bug still there), XPASS (known bug fixed:
remove its KNOWN-BUG line). Exit status 1 on any FAIL or XPASS.
Each case runs in an empty temporary working directory, so cases may write files.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def read_header(path):
    header = {}
    with open(path, encoding='latin-1') as f:
        for line in f:
            m = re.match(r"'\s*([A-Z-]+):\s*(.*?)\s*$", line)
            if not m:
                if not line.startswith("'"):
                    break
                continue
            header[m.group(1)] = m.group(2)
    return header


def parse_items(spec):
    return [tuple(p.split('=', 1)) if '=' in p else (p, 'Wall') for p in spec.split(',') if p.strip()]


def run_wine(binary, path, expect, globals_, items):
    cmd = [binary]
    if expect.startswith('run'):
        cmd.append('--run')
    if globals_:
        cmd.append('--globals=' + globals_)
    tmp = None
    if items:
        tmp = tempfile.TemporaryDirectory()
        stem = os.path.splitext(os.path.basename(path))[0]
        with open(os.path.join(tmp.name, stem + '.items.tsv'), 'w', encoding='utf-8') as f:
            f.writelines(f'{n.strip()}\t{t.strip()}\n' for n, t in parse_items(items))
        cmd += ['--host=' + HERE, '--items=' + tmp.name]
    cmd.append(os.path.abspath(path))
    try:
        with tempfile.TemporaryDirectory() as cwd:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=30, cwd=cwd).stdout
    except subprocess.TimeoutExpired:
        return 'timeout'
    finally:
        if tmp:
            tmp.cleanup()
    line = next((l for l in out.splitlines() if l.startswith(('OK\t', 'FAIL\t'))), '')
    if line.startswith('OK'):
        return 'run-ok' if expect.startswith('run') else 'compile-ok'
    if line.startswith('FAIL'):
        f = line.split('\t')
        return f"{'compile' if f[1] == 'compile' else 'run'}-error {f[4]}"
    return 'crash'


PS32 = os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'SysWOW64', 'WindowsPowerShell', 'v1.0', 'powershell.exe')
SCRIPT_CONTROL = r'''
$sc = New-Object -ComObject MSScriptControl.ScriptControl
$sc.Language = 'VBScript'
foreach ($n in $env:VBS_ITEMS.Split(',')) { if ($n) { $sc.AddObject($n, (New-Object -ComObject Scripting.Dictionary), $false) } }
try { $sc.AddCode([IO.File]::ReadAllText($env:VBS_FILE)) | Out-Null; 'OK' }
catch { 'ERR ' + $sc.Error.Number + ' ' + $sc.Error.Description }
'''


def run_windows_items(path, items):
    env = dict(os.environ, VBS_FILE=path, VBS_ITEMS=','.join(n.strip() for n, _ in parse_items(items)))
    p = subprocess.run([PS32, '-NoProfile', '-Command', SCRIPT_CONTROL], capture_output=True, text=True, timeout=60,
                       env=env, stdin=subprocess.DEVNULL)
    out = next((l for l in p.stdout.splitlines() if l.startswith(('OK', 'ERR'))), p.stdout.strip())
    if out.startswith('OK'):
        return 'ok', out
    m = re.match(r'ERR (\d+)', out)
    num = int(m.group(1)) if m else 0
    return ('compile-error' if 1000 <= num < 1100 else 'run-error'), out


def run_windows(path, items=''):
    if items:
        return run_windows_items(path, items)
    try:
        with tempfile.TemporaryDirectory() as cwd:
            p = subprocess.run(['cscript', '//nologo', '//E:vbscript', os.path.abspath(path)], capture_output=True,
                               text=True, timeout=30, stdin=subprocess.DEVNULL, cwd=cwd)
    except subprocess.TimeoutExpired:
        return 'timeout', ''
    out = (p.stdout + p.stderr).strip()
    if 'compilation error' in out:
        return 'compile-error', out
    if p.returncode != 0 or 'runtime error' in out:
        return 'run-error', out
    return 'ok', out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--engine', choices=('wine', 'windows'), default='wine')
    ap.add_argument('--bin', default=os.environ.get('VBS_CHECK', os.path.expanduser('~/vbs_check_build/vbs_check')))
    ap.add_argument('cases', nargs='*')
    args = ap.parse_args()

    cases = args.cases or sorted(os.path.join(HERE, 'cases', n) for n in os.listdir(os.path.join(HERE, 'cases'))
                                 if n.lower().endswith('.vbs'))
    tally = {}
    for path in cases:
        h = read_header(path)
        name = os.path.basename(path)
        if args.engine == 'windows':
            want = h.get('WINDOWS', '')
            got, out = run_windows(path, h.get('ITEMS', ''))
            status = 'PASS' if got == want else 'FAIL'
            detail = f'want {want}, got {got}' + ('' if status == 'PASS' else f'  [{out[:160]}]')
        else:
            want = h.get('EXPECT', '')
            got = run_wine(args.bin, path, want, h.get('GLOBALS', ''), h.get('ITEMS', ''))
            ok = got == want
            if 'KNOWN-BUG' in h:
                status = 'XPASS' if ok else 'XFAIL'
            else:
                status = 'PASS' if ok else 'FAIL'
            detail = f'want {want}, got {got}'
            if 'KNOWN-BUG' in h:
                detail += '  (known bug ' + h['KNOWN-BUG'].split()[0] + ')'
        tally[status] = tally.get(status, 0) + 1
        print(f'{status:5}  {name:50}  {detail}')
    print('  '.join(f'{k} {v}' for k, v in sorted(tally.items())))
    return 1 if tally.get('FAIL') or tally.get('XPASS') else 0


if __name__ == '__main__':
    sys.exit(main())
