# Checks the Lua lines of lua_lines.h against RQ MOD's rq.lua, then writes a small C file the browser
# is built with (so the check runs on each build that has rq.lua), and a make-style dependency file
# naming both inputs (rq.lua is outside the project in RQ MOD's build). Without rq.lua (the browser
# built alone) nothing is checked and the count is 0.
#
# Each macro has a comment before it. A call's comment starts with the call and its parameters,
# "rq.monster.model(def): ...", and a fragment's (a part of a line the browser puts together) with
# the class its keys belong to, "rq.TextureColors: ...". What is checked:
# - each rq.<module>.<call>( in a macro is a function of rq.lua;
# - each key "name =" of the outermost table given to a call is a field of that parameter's type in
#   rq.lua (its ---@param: a class, with its parents' fields, or an inline {key?: type, ...}), and
#   each key of a fragment a field of the class its comment names;
# - a table inside it is checked only when the comment names its class, "textures[*]: rq.TextureColors"
#   (each table among the values of the table at textures) or "opts: rq.Class" (the table itself);
#   ["..."] = keys are not checked;
# - every class a comment names is in rq.lua.
# Usage: check_lua_lines.py OUT.c DEPFILE lua_lines.h [rq.lua]
import re
import sys


def depfile_path(p):
    return p.replace("\\", "/").replace(" ", "\\ ")


def c_strings(value):
    """The text of a macro's adjacent C string literals."""
    out = []
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', value):
        out.append(re.sub(r'\\(.)', lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), lit))
    return "".join(out)


def macros(text):
    """(comment, name, text) of each #define with a comment before it."""
    found = []
    for m in re.finditer(r'/\*((?:(?!\*/).)*)\*/\s*#define\s+(\w+)[ \t]+((?:"(?:[^"\\\n]|\\.)*"[ \t]*)+)', text, re.S):
        found.append((" ".join(m.group(1).split()), m.group(2), c_strings(m.group(3))))
    return found


def skip_string(s, i):
    q = s[i]
    i += 1
    while i < len(s) and s[i] != q:
        i += 2 if s[i] == "\\" else 1
    return i + 1


def closing(s, i):
    """The index past the bracket that closes the one at s[i] ((, { or [)."""
    pairs = {"(": ")", "{": "}", "[": "]"}
    stack = [pairs[s[i]]]
    i += 1
    while i < len(s) and stack:
        c = s[i]
        if c in "\"'":
            i = skip_string(s, i)
            continue
        if c in pairs:
            stack.append(pairs[c])
        elif stack and c == stack[-1]:
            stack.pop()
        i += 1
    return i


def split_top(s):
    """The items of a list's text, split at its top-level commas."""
    items, depth, start, i = [], 0, 0, 0
    while i < len(s):
        c = s[i]
        if c in "\"'":
            i = skip_string(s, i)
            continue
        if c in "({[":
            depth += 1
        elif c in ")}]":
            depth -= 1
        elif c == "," and depth == 0:
            items.append(s[start:i])
            start = i + 1
        i += 1
    if s[start:].strip():
        items.append(s[start:])
    return items


def table_entries(t):
    """(key, value) of a table literal's text "{ ... }"; key None for an item without one and for
    a ["..."] key."""
    body = t.strip()[1:-1]
    out = []
    for item in split_top(body):
        m = re.match(r'\s*([A-Za-z_]\w*)\s*=(?!=)\s*(.*)$', item, re.S)
        if m:
            out.append((m.group(1), m.group(2).strip()))
        else:
            m = re.match(r'\s*\[.*?\]\s*=(?!=)\s*(.*)$', item, re.S)
            out.append((None, (m.group(1) if m else item).strip()))
    return out


def calls(text):
    """(module, name, [argument texts]) of each rq.<module>.<name>( in text."""
    out = []
    for m in re.finditer(r'rq\.([a-z_]+)\.([a-z_]+)\(', text):
        start = m.end() - 1
        end = closing(text, start)
        out.append((m.group(1), m.group(2), split_top(text[start + 1:end - 1])))
    return out


class Lua:
    def __init__(self, text):
        self.text = text
        self.classes = {}   # name -> (parents, fields)
        lines = text.splitlines()
        i = 0
        while i < len(lines):
            m = re.match(r'---@class\s+([\w.]+)\s*(?::\s*(.*))?$', lines[i])
            if not m:
                i += 1
                continue
            parents = [p.strip() for p in (m.group(2) or "").split(",") if p.strip()]
            fields = set()
            i += 1
            while i < len(lines) and lines[i].startswith("---"):
                f = re.match(r'---@field\s+(\w+)', lines[i])
                if f:
                    fields.add(f.group(1))
                if lines[i].startswith("---@class"):
                    break
                i += 1
            self.classes[m.group(1)] = (parents, fields)

    def has_function(self, module, name):
        return re.search(r"^function %s\.%s\(" % (re.escape(module), re.escape(name)), self.text, re.M) is not None

    def fields(self, cls, seen=None):
        seen = seen or set()
        if cls in seen or cls not in self.classes:
            return set()
        seen.add(cls)
        parents, fields = self.classes[cls]
        out = set(fields)
        for p in parents:
            out |= self.fields(p, seen)
        return out

    def params(self, module, name):
        """The function's parameters, in order, each (name, type text)."""
        m = re.search(r"^function %s\.%s\(([^)]*)\)" % (re.escape(module), re.escape(name)), self.text, re.M)
        if not m:
            return []
        names = [p.strip() for p in m.group(1).split(",") if p.strip()]
        before = self.text[:m.start()].splitlines()
        doc = []
        for line in reversed(before):
            if not line.startswith("---"):
                break
            doc.append(line)
        types = {}
        for line in doc:
            p = re.match(r'---@param\s+(\w+)\??\s+(.*)$', line)
            if not p:
                continue
            rest = p.group(2)
            if rest.startswith("{"):
                typ = rest[:closing(rest, 0)]
            else:
                typ = rest.split()[0] if rest.split() else ""
            types[p.group(1)] = typ
        return [(n, types.get(n, "")) for n in names]


def type_keys(lua, typ):
    """The keys a type takes: an inline {key?: type} table's, or a class's among a union's members;
    None when it is neither (nothing to check)."""
    typ = typ.strip()
    if typ.startswith("{"):
        keys = set()
        for item in split_top(typ[1:-1]):
            k = re.match(r'\s*(\w+)\??\s*:', item)
            if k:
                keys.add(k.group(1))
        return keys
    for member in typ.split("|"):
        member = member.strip()
        if member.startswith("rq.") and not member.endswith("]") and member in lua.classes:
            return lua.fields(member)
    return None


def check_table(lua, t, keys, what, specs, path, problems, counted):
    """The keys of table literal t against keys; then the tables inside it that the comment names."""
    for key, value in table_entries(t):
        if key is not None:
            counted[0] += 1
            if key not in keys:
                problems.append("%s: %s is not a field of %s" % (what, key, path))
        if key is None or not value.startswith("{"):
            continue
        if key in specs:
            check_table(lua, value, lua.fields(specs[key]), what, {}, specs[key], problems, counted)
        if key + "[*]" in specs:
            cls = specs[key + "[*]"]
            for _, inner in table_entries(value):
                if inner.startswith("{"):
                    check_table(lua, inner, lua.fields(cls), what, {}, cls, problems, counted)


def main():
    out, dep, header = sys.argv[1], sys.argv[2], sys.argv[3]
    rq_lua = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] else None
    text = open(header, encoding="utf-8").read()
    found_calls = sorted(set(re.findall(r"rq\.([a-z_]+)\.([a-z_]+)\(", text)))
    checked = 0
    if rq_lua:
        lua = Lua(open(rq_lua, encoding="utf-8").read())
        problems = []
        missing = ["rq.%s.%s" % c for c in found_calls if not lua.has_function(*c)]
        if missing:
            problems.append("calls rq.lua does not have: %s" % ", ".join(missing))
        counted = [0]
        for comment, name, value in macros(text):
            specs = dict(re.findall(r'(\w+(?:\[\*\])?)\s*:\s*(rq\.[A-Z]\w*)', comment))
            for cls in set(specs.values()):
                if cls not in lua.classes:
                    problems.append("%s: its comment names %s, which rq.lua does not have" % (name, cls))
            frag = re.match(r'(rq\.[A-Z]\w*)\b', comment)
            if frag:
                cls = frag.group(1)
                if cls not in lua.classes:
                    problems.append("%s: its comment names %s, which rq.lua does not have" % (name, cls))
                    continue
                check_table(lua, "{" + value + "}", lua.fields(cls), name, specs, cls, problems, counted)
                continue
            head = re.match(r'rq\.([a-z_]+)\.([a-z_]+)\(([^)]*)\)', comment)
            for module, fn, args in calls(value):
                if not lua.has_function(module, fn):
                    continue
                params = lua.params(module, fn)
                shown = [p.strip() for p in head.group(3).split(",")] if head and head.group(1) == module and head.group(2) == fn else []
                for i, arg in enumerate(args):
                    arg = arg.strip()
                    if not arg.startswith("{") or i >= len(params):
                        continue
                    pname = shown[i] if i < len(shown) and shown[i] else params[i][0]
                    if pname in specs:
                        keys, path = lua.fields(specs[pname]), specs[pname]
                    else:
                        keys, path = type_keys(lua, params[i][1]), params[i][1]
                    if keys is None:
                        continue
                    check_table(lua, arg, keys, name, specs, "rq.%s.%s's %s (%s)" % (module, fn, pname, path), problems, counted)
        if problems:
            sys.stderr.write("lua_lines.h against rq.lua:\n  %s\n" % "\n  ".join(problems))
            sys.exit(1)
        checked = len(found_calls)
        sys.stdout.write("check_lua_lines.py: %d calls and %d fields checked against rq.lua\n" % (checked, counted[0]))
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* generated by check_lua_lines.py: %d calls checked against rq.lua */\n" % checked)
        f.write("const int rq_lua_lines_checked = %d;\n" % checked)
    with open(dep, "w", encoding="utf-8", newline="\n") as f:
        f.write("%s: %s%s\n" % (depfile_path(out), depfile_path(header), " " + depfile_path(rq_lua) if rq_lua else ""))


main()
