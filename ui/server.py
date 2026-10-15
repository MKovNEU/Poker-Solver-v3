#!/usr/bin/env python3
"""Web UI for the postflop and preflop solvers.

    python ui/server.py [--port 8000]

then open http://localhost:8000. The page talks to this server over HTTP, the server runs `solver <config> --serve`
(include/solver/protocol.h) and `solver <config> --preflop --serve` (include/preflop/protocol.h), one of each at
most, and relays their JSON lines. Solved preflop games are cached in ui/work/preflop. Standard library only.
"""

import argparse
import atexit
import ctypes
import hashlib
import json
import mimetypes
import os
import re
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATIC = os.path.join(ROOT, "ui", "static")
WORK = os.path.join(ROOT, "ui", "work")
EXAMPLES = os.path.join(ROOT, "examples")
SOLVER = os.path.join(ROOT, "solver.exe" if os.name == "nt" else "solver")
# solver.exe needs the MinGW runtime DLLs (libgomp, libstdc++), see the build notes in the README
DLL_DIR = os.environ.get("SOLVER_DLL_DIR", r"C:\msys64\ucrt64\bin")
KINDS = ("postflop", "preflop")
PATH_TOKEN = {"postflop": re.compile(r"^(\d{1,3}|[2-9TJQKA][cdhs])$"), "preflop": re.compile(r"^\d{1,2}$")}


def free_memory_mb():
    if os.name == "nt":
        class MemoryStatus(ctypes.Structure):
            _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong), ("total_phys", ctypes.c_ulonglong),
                        ("avail_phys", ctypes.c_ulonglong), ("total_page", ctypes.c_ulonglong),
                        ("avail_page", ctypes.c_ulonglong), ("total_virtual", ctypes.c_ulonglong),
                        ("avail_virtual", ctypes.c_ulonglong), ("avail_extended", ctypes.c_ulonglong)]

        status = MemoryStatus()
        status.length = ctypes.sizeof(MemoryStatus)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return status.avail_phys / 2**20
        return None
    try:
        return os.sysconf("SC_AVPHYS_PAGES") * os.sysconf("SC_PAGE_SIZE") / 2**20
    except (ValueError, OSError, AttributeError):
        return None


def preflop_cache(config_text):
    return os.path.join(WORK, "preflop", hashlib.sha1(config_text.encode()).hexdigest()[:16] + ".sol")


class SolverError(Exception):
    pass


class Session:
    """One `solver --serve` process: a built tree, its solve and the strategy queries on it."""

    def __init__(self, kind, config_text):
        if not os.path.exists(SOLVER):
            raise SolverError(f"{SOLVER} not found, build it first (mingw32-make)")
        self.kind = kind
        self.config_text = config_text
        os.makedirs(WORK, exist_ok=True)
        args = [SOLVER]
        self.cache = preflop_cache(config_text) if kind == "preflop" else None
        if self.cache and os.path.exists(self.cache):
            args += ["--load", self.cache]  # solved before
        else:
            path = os.path.join(WORK, f"{kind}.txt")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(config_text)
            args.append(path)
        if kind == "preflop":
            args.append("--preflop")
        args.append("--serve")
        env = dict(os.environ)
        if os.path.isdir(DLL_DIR):
            env["PATH"] = DLL_DIR + os.pathsep + env.get("PATH", "")
        self.proc = subprocess.Popen(args, cwd=ROOT, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True, bufsize=1)
        self.cond = threading.Condition()
        self.query_lock = threading.Lock()  # one question to the solver at a time
        self.state = "building"  # building, ready, solving, solved, failed, closed
        self.message = None      # what a slow build is doing
        self.error = None
        self.tree = None
        self.result = None
        self.progress = []       # every exploitability check, across solve / continue
        self.iteration = 0
        self.seconds = 0.0
        self.replies = []
        self.stderr = []
        self.stderr_reader = threading.Thread(target=self._read_stderr, daemon=True)
        self.stderr_reader.start()
        threading.Thread(target=self._read_stdout, daemon=True).start()

    def wait_built(self, timeout=600):
        with self.cond:
            self.cond.wait_for(lambda: self.state != "building", timeout=timeout)
            if self.state not in ("ready", "solved"):
                self.proc.kill()
                raise SolverError(self.error or "building the tree timed out")

    def _read_stderr(self):
        for line in self.proc.stderr:
            self.stderr.append(line)

    def _read_stdout(self):
        for line in self.proc.stdout:
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            kind = msg.get("type")
            with self.cond:
                if kind == "status":
                    self.message = msg["message"]
                elif kind == "tree":
                    self.tree = msg
                    self.iteration = msg["iterations_done"]
                    self.message = None
                    if self.iteration > 0:  # a cached solution
                        self.result = {"iterations": self.iteration, "cached": True, "stopped": False}
                        self.state = "solved"
                    else:
                        self.state = "ready"
                elif kind == "iteration":
                    self.iteration, self.seconds = msg["iteration"], msg["seconds"]
                elif kind == "progress":
                    self.progress.append(msg)
                    self.iteration, self.seconds = msg["iteration"], msg["seconds"]
                elif kind == "solved":
                    self.result = msg
                    self.iteration, self.seconds = msg["iterations"], msg["seconds"]
                    self.state = "solved"
                elif kind == "error" and self.state == "solving":
                    self.error = msg["message"]
                    self.state = "solved" if self.result else "ready"
                else:
                    self.replies.append(msg)
                self.cond.notify_all()
        self.proc.wait()
        self.stderr_reader.join(timeout=2)
        with self.cond:
            if self.state != "closed":
                lines = [s.strip() for s in self.stderr if s.strip()]
                self.error = lines[-1] if lines else f"the solver exited unexpectedly (code {self.proc.returncode})"
                if self.error.startswith("error: "):
                    self.error = self.error[len("error: "):]
                self.state = "failed"
            self.cond.notify_all()

    def _send(self, line):
        try:
            self.proc.stdin.write(line + "\n")
            self.proc.stdin.flush()
        except OSError as e:
            raise SolverError(f"the solver is not running ({e})") from e

    def solve(self, iterations, target):
        with self.query_lock, self.cond:
            if self.state not in ("ready", "solved"):
                raise SolverError(f"cannot solve, the solver is {self.state}")
            self.state = "solving"
            self.error = None
            self._send(f"solve {int(iterations)} {float(target)}")

    def stop(self):
        self._send("stop")

    def query(self, command, timeout=900):
        with self.query_lock:
            with self.cond:
                if self.state not in ("ready", "solved"):
                    raise SolverError(f"the solver is {self.state}")
                self.replies.clear()
                self._send(command)
                self.cond.wait_for(lambda: self.replies or self.state == "failed", timeout)
                if not self.replies:
                    raise SolverError(self.error or "no answer from the solver")
                reply = self.replies.pop(0)
        if reply.get("type") == "error":
            raise SolverError(reply["message"])
        return reply

    def status(self, since):
        with self.cond:
            return {"state": self.state, "message": self.message, "error": self.error, "tree": self.tree,
                    "result": self.result, "config": self.config_text, "iteration": self.iteration,
                    "seconds": self.seconds, "progress": self.progress[since:], "progress_count": len(self.progress)}

    def close(self):
        with self.cond:
            self.state = "closed"
        self.proc.kill()  # nothing to save, and it frees the solver's memory right away
        self.proc.wait()


SESSIONS = {kind: None for kind in KINDS}
SESSION_LOCK = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send_json(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path.startswith("/api/"):
            self.api(url.path[5:], parse_qs(url.query), None)
        else:
            self.static_file(url.path)

    def do_POST(self):
        url = urlparse(self.path)
        length = int(self.headers.get("Content-Length") or 0)
        try:
            body = json.loads(self.rfile.read(length) or b"{}")
        except ValueError:
            return self.send_json(400, {"error": "bad JSON"})
        self.api(url.path[5:] if url.path.startswith("/api/") else "", parse_qs(url.query), body)

    def static_file(self, path):
        name = "index.html" if path in ("", "/") else path.lstrip("/")
        full = os.path.normpath(os.path.join(STATIC, name))
        if not full.startswith(STATIC + os.sep) or not os.path.isfile(full):
            return self.send_error(404)
        with open(full, "rb") as f:
            data = f.read()
        kind = mimetypes.guess_type(full)[0] or "application/octet-stream"
        if kind.startswith("text/") or kind.endswith("javascript"):
            kind += "; charset=utf-8"
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def api(self, name, query, body):
        post = body is not None
        kind = query.get("kind", ["postflop"])[0]
        if kind not in KINDS:
            return self.send_json(400, {"error": f"unknown kind '{kind}'"})
        try:
            if name == "info":
                return self.send_json(200, {"solver": os.path.exists(SOLVER), "free_memory_mb": free_memory_mb(),
                                            "cpus": os.cpu_count()})
            if name == "examples":
                examples = {}
                for file in sorted(os.listdir(EXAMPLES)):
                    if file.endswith(".txt"):
                        with open(os.path.join(EXAMPLES, file), encoding="utf-8") as f:
                            examples[file[:-4]] = f.read()
                return self.send_json(200, examples)
            if name == "build" and post:
                with SESSION_LOCK:
                    if SESSIONS[kind]:
                        SESSIONS[kind].close()
                        SESSIONS[kind] = None
                    session = SESSIONS[kind] = Session(kind, str(body["config"]))
                if kind == "preflop":  # can take a minute the first time (equity table), the page polls status
                    return self.send_json(200, {"state": session.state})
                session.wait_built()
                return self.send_json(200, {"tree": session.tree, "free_memory_mb": free_memory_mb()})

            session = SESSIONS[kind]
            if name == "status":
                if session is None:
                    return self.send_json(200, {"state": "none", "tree": None, "progress": []})
                return self.send_json(200, session.status(int(query.get("since", ["0"])[0])))
            if session is None:
                return self.send_json(409, {"error": "build a tree first"})
            if name == "solve" and post:
                session.solve(body["iterations"], body["target"])
                return self.send_json(200, {"ok": True})
            if name == "stop" and post:
                session.stop()
                return self.send_json(200, {"ok": True})
            if name == "save" and post and kind == "preflop":
                os.makedirs(os.path.dirname(session.cache), exist_ok=True)
                return self.send_json(200, session.query(f"save {session.cache}"))
            if name == "node" or (name == "ev" and kind == "postflop"):
                tokens = [t for t in query.get("path", [""])[0].split(",") if t]
                if not all(PATH_TOKEN[kind].match(t) for t in tokens):
                    return self.send_json(400, {"error": "bad path"})
                return self.send_json(200, session.query(" ".join([name] + tokens)))
            return self.send_json(404, {"error": f"unknown endpoint '{name}'"})
        except SolverError as e:
            return self.send_json(400, {"error": str(e)})
        except (KeyError, ValueError, TypeError) as e:
            return self.send_json(400, {"error": f"bad request: {e}"})


def close_all():
    for session in SESSIONS.values():
        if session:
            session.close()


def main():
    parser = argparse.ArgumentParser(description="web UI for the postflop and preflop solvers")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--host", default="127.0.0.1", help="interface to listen on (default: this machine only)")
    args = parser.parse_args()
    atexit.register(close_all)
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"solver UI on http://localhost:{args.port}  (Ctrl+C to quit)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
