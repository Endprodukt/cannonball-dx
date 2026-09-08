from pathlib import Path

path = Path("src/main/frontend/ttrial.cpp")
text = path.read_text(encoding="utf-8")

old = "#include <SDL.h>\n#include <string>\n"
new = "#include <SDL.h>\n#include <string>\n#include <algorithm>\n"

if new in text:
    print("ttrial.cpp already has <algorithm>.")
elif old in text:
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("Added <algorithm> to ttrial.cpp.")
else:
    raise SystemExit("Could not locate ttrial.cpp include block")
