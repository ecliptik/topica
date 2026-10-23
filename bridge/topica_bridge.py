"""topica-bridge: where Topica's Post > Publish sends a post.

A classic Macintosh has no TLS, so it can't push to a git host itself. This small server listens on the LAN in plain
HTTP/1.0 and takes one request:

    POST /publish
    X-Topica-Key: <the shared key>
    X-Topica-Path: _posts/2026-10-23-hello-world.md
    Content-Length: <n>

    <the post, UTF-8>

It writes the post into its own clone of the blog's repository, commits it on the drafts branch (made from the base
branch the first time) and pushes. Nothing is ever pushed anywhere else: the blog's maintainer merges the drafts
branch (or opens a pull request from it) when they want the post on the site.

The reply is one line of plain ASCII text for the Mac to show, with the status:
    200  "Published _posts/x.md on drafts (1a2b3c4)." or "drafts already has this version of _posts/x.md."
    400  a path outside TOPICA_BRIDGE_DIRS, a body that isn't UTF-8 text, a malformed request
    401  no key, or the wrong one          403  a source address off TOPICA_BRIDGE_ALLOW
    404  another path                      405  another method
    411  no Content-Length                 413  a post over TOPICA_BRIDGE_MAX_BYTES
    502  git couldn't fetch or push (the remote's own words are logged, not sent)

The log has the source, method, path, status and size: never the key or the post.

Standard library only. Settings (environment):
    TOPICA_BRIDGE_REPO      the clone the bridge owns (required): its working tree is reset on every request
    TOPICA_BRIDGE_KEY_FILE  a file holding the shared key (or TOPICA_BRIDGE_KEY, the key itself); at least 16 characters
    TOPICA_BRIDGE_BIND      the address to listen on (127.0.0.1); TOPICA_BRIDGE_PORT the port (8023)
    TOPICA_BRIDGE_ALLOW     source addresses or networks admitted, comma-separated, or "private" (the default:
                            loopback and the private ranges, as Python's ipaddress has them)
    TOPICA_BRIDGE_REMOTE    the remote to fetch and push (origin)
    TOPICA_BRIDGE_BRANCH    the drafts branch (drafts); TOPICA_BRIDGE_BASE the branch it starts from (main)
    TOPICA_BRIDGE_DIRS      the folders a post may go in (_posts,_drafts,content/posts)
    TOPICA_BRIDGE_MAX_BYTES the largest post (524288); TOPICA_BRIDGE_TIMEOUT git's timeout in seconds (60)
"""
import hmac
import http.server
import ipaddress
import os
import re
import subprocess
import sys
import threading
import time

VERSION = "0.1.0"

NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}\.(md|markdown)\Z")
DATED_RE = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}-")    # Jekyll skips a post in _posts without its date


class Config:
    def __init__(self, env):
        self.repo = env.get("TOPICA_BRIDGE_REPO", "")
        if not self.repo or not os.path.isdir(os.path.join(self.repo, ".git")):
            raise ValueError("TOPICA_BRIDGE_REPO must be a git clone")
        key = env.get("TOPICA_BRIDGE_KEY", "")
        if env.get("TOPICA_BRIDGE_KEY_FILE"):
            with open(env["TOPICA_BRIDGE_KEY_FILE"], encoding="ascii") as f:
                key = f.read().strip()
        if len(key) < 16 or not key.isascii() or not key.isprintable():
            raise ValueError("the key (TOPICA_BRIDGE_KEY_FILE) must be at least 16 printable ASCII characters")
        self.key = key.encode("ascii")
        self.bind = env.get("TOPICA_BRIDGE_BIND", "127.0.0.1")
        self.port = int(env.get("TOPICA_BRIDGE_PORT", "8023"))
        allow = env.get("TOPICA_BRIDGE_ALLOW", "private").strip()
        self.allow = None if allow == "private" else [ipaddress.ip_network(n.strip()) for n in allow.split(",")
                                                       if n.strip()]
        self.remote = env.get("TOPICA_BRIDGE_REMOTE", "origin")
        self.branch = env.get("TOPICA_BRIDGE_BRANCH", "drafts")
        self.base = env.get("TOPICA_BRIDGE_BASE", "main")
        self.dirs = [d.strip().strip("/") for d in env.get("TOPICA_BRIDGE_DIRS", "_posts,_drafts,content/posts").split(",")
                     if d.strip()]
        self.max_bytes = int(env.get("TOPICA_BRIDGE_MAX_BYTES", "524288"))
        self.timeout = int(env.get("TOPICA_BRIDGE_TIMEOUT", "60"))


def valid_path(path, dirs):
    """The post's path if it names a Markdown file directly in one of dirs (dated in _posts), else None."""
    folder, _, name = path.rpartition("/")
    if folder in dirs and NAME_RE.match(name) and ".." not in name and (folder != "_posts" or DATED_RE.match(name)):
        return path
    return None


class GitError(Exception):
    pass


class Publisher:
    """Commits posts on the drafts branch, one at a time."""

    def __init__(self, config):
        self.c = config
        self.lock = threading.Lock()

    def git(self, *args):
        env = dict(os.environ, GIT_TERMINAL_PROMPT="0", LC_ALL="C")
        r = subprocess.run(["git", "-C", self.c.repo, *args], capture_output=True, text=True, env=env,
                           timeout=self.c.timeout)
        if r.returncode != 0:
            raise GitError(f"git {args[0]}: {(r.stderr or r.stdout).strip()}")
        return r.stdout.strip()

    def has_remote_branch(self, name):
        try:
            self.git("rev-parse", "--verify", "--quiet", f"refs/remotes/{self.c.remote}/{name}")
            return True
        except GitError:
            return False

    def publish(self, path, body):
        """Returns the line for the Mac. Raises GitError when the remote can't be reached or refuses."""
        with self.lock:
            for attempt in (1, 2):
                self.git("fetch", "--quiet", "--prune", self.c.remote)
                start = self.c.branch if self.has_remote_branch(self.c.branch) else self.c.base
                self.git("checkout", "--quiet", "--force", "-B", self.c.branch, f"{self.c.remote}/{start}")
                self.git("clean", "--quiet", "-fdx")
                full = os.path.join(self.c.repo, path)
                os.makedirs(os.path.dirname(full), exist_ok=True)
                with open(full, "wb") as f:
                    f.write(body)
                self.git("add", "--", path)
                if subprocess.run(["git", "-C", self.c.repo, "diff", "--cached", "--quiet"]).returncode == 0:
                    if start == self.c.branch:
                        return f"{self.c.branch} already has this version of {path}."
                    # a new drafts branch that matches the base: push it anyway, so it exists
                self.git("commit", "--quiet", "--allow-empty", "-m", f"Publish {path} from Topica")
                commit = self.git("rev-parse", "--short=7", "HEAD")
                try:
                    self.git("push", "--quiet", self.c.remote, f"HEAD:refs/heads/{self.c.branch}")
                except GitError:
                    if attempt == 2:
                        raise
                    continue            # someone pushed in between: start again from theirs
                return f"Published {path} on {self.c.branch} ({commit})."


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = f"topica-bridge/{VERSION}"
    sys_version = ""
    protocol_version = "HTTP/1.0"

    def reply(self, status, text):
        data = (text + "\n").encode("ascii", "replace")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=us-ascii")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)
        self.size = len(data)

    def log_request(self, code="-", size="-"):
        pass                            # logged once, in log_done

    def log_done(self, status):
        sys.stderr.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {self.client_address[0]} {self.command} "
                         f"{self.path.split('?')[0][:64]} {status} {getattr(self, 'size', 0)}\n")

    def admitted(self):
        try:
            ip = ipaddress.ip_address(self.client_address[0])
        except ValueError:
            return False
        if self.server.config.allow is None:
            return ip.is_private
        return any(ip in n for n in self.server.config.allow)

    def handle_one(self):
        c = self.server.config
        if not self.admitted():
            return 403, "This address may not publish."
        if self.command != "POST":
            return 405, "Only POST /publish."
        if self.path != "/publish":
            return 404, "Only POST /publish."
        key = self.headers.get("X-Topica-Key", "").encode("ascii", "replace")
        if not hmac.compare_digest(key, c.key):
            return 401, "The bridge doesn't know that key: check the key in Publish."
        length = self.headers.get("Content-Length")
        if length is None or not length.isdigit():
            return 411, "The post's length is missing."
        if int(length) > c.max_bytes:
            return 413, f"The post is over {c.max_bytes // 1024}K."
        path = valid_path(self.headers.get("X-Topica-Path", ""), c.dirs)
        if path is None:
            return 400, f"A post goes in {', '.join(c.dirs)}, named like 2026-10-23-title.md."
        body = self.rfile.read(int(length))
        if len(body) != int(length):
            return 400, "The post was cut off."
        try:
            body.decode("utf-8")
        except UnicodeDecodeError:
            return 400, "The post isn't UTF-8 text."
        if b"\0" in body:
            return 400, "The post isn't text."
        try:
            return 200, self.server.publisher.publish(path, body)
        except (GitError, subprocess.TimeoutExpired, OSError) as e:
            sys.stderr.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {e}\n")
            return 502, "The bridge couldn't push the post: see its log."

    def handle_any(self):
        status, text = self.handle_one()
        self.reply(status, text)
        self.log_done(status)

    do_POST = do_GET = do_PUT = do_DELETE = do_HEAD = handle_any


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, config):
        self.config = config
        self.publisher = Publisher(config)
        super().__init__((config.bind, config.port), Handler)


def main():
    try:
        config = Config(os.environ)
    except (ValueError, OSError) as e:
        sys.exit(f"topica-bridge: {e}")
    server = Server(config)
    sys.stderr.write(f"topica-bridge {VERSION} on {config.bind}:{server.server_address[1]}, "
                     f"pushing {config.remote} {config.branch}\n")
    server.serve_forever()


if __name__ == "__main__":
    main()
