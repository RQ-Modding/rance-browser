# Turns a Lua source file into a C array, so the loader carries it inside the exe.
# Usage: gen_lua_embed.py IN.lua OUT.c NAME  ->  const char NAME[]; const unsigned NAME_len;
import sys

src, out, name = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(src, "rb").read()
with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write("/* generated from %s at compile, do not edit */\n" % src.replace("\\", "/").split("/")[-1])
    f.write("const unsigned %s_len = %d;\n" % (name, len(data)))
    f.write("const char %s[] = {\n" % name)
    for i in range(0, len(data), 16):
        f.write("\t" + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",\n")
    f.write("\t0\n};\n")
