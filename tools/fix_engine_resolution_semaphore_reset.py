from pathlib import Path

path = Path("src/main/main.cpp")
text = path.read_text(encoding="utf-8")

old = '''        if (t0.joinable()) t0.join();\n        if (t1.joinable()) t1.join();\n        if (t2.joinable()) t2.join();\n    };\n'''

new = '''        if (t0.joinable()) t0.join();\n        if (t1.joinable()) t1.join();\n        if (t2.joinable()) t2.join();\n\n        // A worker can observe running=false before consuming the wake-up\n        // release above. Drain every semaphore after join so a newly-created\n        // worker can never inherit a stale permit or completion token from the\n        // previous renderer generation.\n        while (prepareReady.try_acquire()) {}\n        while (renderReady0.try_acquire()) {}\n        while (renderReady1.try_acquire()) {}\n        while (prepareDone.try_acquire()) {}\n        while (renderDone0.try_acquire()) {}\n        while (renderDone1.try_acquire()) {}\n    };\n'''

if new in text:
    print("Semaphore reset already applied")
elif old in text:
    text = text.replace(old, new, 1)
    path.write_text(text, encoding="utf-8")
    print("Applied clean semaphore reset after worker shutdown")
else:
    raise SystemExit("quiescent worker shutdown block not found")
