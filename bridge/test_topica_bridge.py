"""Tests for topica_bridge: a bare repository in a temporary folder plays the blog's remote, on loopback only.

    python3 -m unittest discover -s bridge
"""
import http.client
import os
import socket
import subprocess
import tempfile
import threading
import unittest

import topica_bridge

KEY = "k" * 24


def git(cwd, *args):
    return subprocess.run(["git", "-C", cwd, *args], check=True, capture_output=True, text=True).stdout.strip()


class BridgeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.remote = os.path.join(t, "blog.git")
        subprocess.run(["git", "init", "--quiet", "--bare", "-b", "main", self.remote], check=True)
        seed = os.path.join(t, "seed")
        subprocess.run(["git", "clone", "--quiet", self.remote, seed], check=True, capture_output=True)
        for k, v in (("user.name", "Test"), ("user.email", "test@example.com")):
            git(seed, "config", k, v)
        os.makedirs(os.path.join(seed, "_posts"))
        with open(os.path.join(seed, "_posts", "2026-01-01-old.md"), "w") as f:
            f.write("---\ntitle: Old\n---\n")
        git(seed, "add", ".")
        git(seed, "commit", "--quiet", "-m", "seed")
        git(seed, "push", "--quiet", "origin", "main")
        self.clone = os.path.join(t, "clone")
        subprocess.run(["git", "clone", "--quiet", self.remote, self.clone], check=True, capture_output=True)
        for k, v in (("user.name", "Topica Bridge"), ("user.email", "bridge@example.com")):
            git(self.clone, "config", k, v)
        self.start({})

    def start(self, extra):
        env = {"TOPICA_BRIDGE_REPO": self.clone, "TOPICA_BRIDGE_KEY": KEY, "TOPICA_BRIDGE_PORT": "0", **extra}
        self.server = topica_bridge.Server(topica_bridge.Config(env))
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.tmp.cleanup()

    def send(self, body=b"---\ntitle: Hello\n---\n\nHi.\n", path="_posts/2026-10-23-hello.md", key=KEY,
             method="POST", target="/publish", headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.server.server_address[1], timeout=30)
        h = {"X-Topica-Key": key, "X-Topica-Path": path}
        h.update(headers or {})
        conn.request(method, target, body=body, headers=h)
        r = conn.getresponse()
        text = r.read().decode("ascii")
        conn.close()
        return r.status, text.strip()

    def remote_file(self, branch, path):
        return subprocess.run(["git", "-C", self.remote, "show", f"{branch}:{path}"], capture_output=True).stdout

    def test_publish_makes_the_drafts_branch(self):
        status, text = self.send()
        self.assertEqual(status, 200, text)
        self.assertRegex(text, r"^Published _posts/2026-10-23-hello\.md on drafts \([0-9a-f]{7}\)\.$")
        self.assertEqual(self.remote_file("drafts", "_posts/2026-10-23-hello.md"), b"---\ntitle: Hello\n---\n\nHi.\n")
        self.assertEqual(self.remote_file("drafts", "_posts/2026-01-01-old.md"), b"---\ntitle: Old\n---\n")
        self.assertEqual(self.remote_file("main", "_posts/2026-10-23-hello.md"), b"")     # main is never touched

    def test_again_and_changed(self):
        self.send()
        status, text = self.send()
        self.assertEqual((status, text), (200, "drafts already has this version of _posts/2026-10-23-hello.md."))
        status, text = self.send(body="Café — changed.\n".encode("utf-8"))
        self.assertEqual(status, 200, text)
        self.assertEqual(self.remote_file("drafts", "_posts/2026-10-23-hello.md"), "Café — changed.\n".encode())
        log = git(self.remote, "log", "--format=%s", "drafts")
        self.assertEqual(log.splitlines()[:2], ["Publish _posts/2026-10-23-hello.md from Topica"] * 2)

    def test_drafts_branch_moved_on_elsewhere(self):
        self.send()
        other = os.path.join(self.tmp.name, "other")
        subprocess.run(["git", "clone", "--quiet", "-b", "drafts", self.remote, other], check=True, capture_output=True)
        git(other, "config", "user.name", "O")
        git(other, "config", "user.email", "o@example.com")
        with open(os.path.join(other, "notes.txt"), "w") as f:
            f.write("theirs\n")
        git(other, "add", ".")
        git(other, "commit", "--quiet", "-m", "theirs")
        git(other, "push", "--quiet", "origin", "drafts")
        status, text = self.send(path="_drafts/second.md")
        self.assertEqual(status, 200, text)
        self.assertEqual(self.remote_file("drafts", "notes.txt"), b"theirs\n")      # built on theirs, not forced
        self.assertNotEqual(self.remote_file("drafts", "_drafts/second.md"), b"")

    def test_refusals(self):
        self.assertEqual(self.send(key="wrong" * 5)[0], 401)
        self.assertEqual(self.send(key="")[0], 401)
        for bad in ("../x.md", "_posts/../x.md", "_posts/x.txt", "_posts/sub/x.md", "_config.yml", "x.md",
                    "/_posts/x.md", "_posts/.hidden.md", "_posts/", "", "_posts/undated.md",
                    "_posts/2025/2026-10-23-x.md", "_posts/2026/x.md", "_posts/2026/10/2026-10-23-x.md",
                    "_posts/20266/20266-10-23-x.md", "_drafts/2026/2026-10-23-x.md"):
            self.assertEqual(self.send(path=bad)[0], 400, bad)
        self.assertEqual(self.send(body=b"\xff\xfe bad")[0], 400)
        self.assertEqual(self.send(body=b"nul\0")[0], 400)
        self.assertEqual(self.send(method="GET", body=None)[0], 405)
        self.assertEqual(self.send(target="/publish/../x")[0], 404)
        self.assertEqual(self.send(target="/publish?x=1")[0], 404)
        self.assertEqual(self.remote_file("drafts", "_posts/2026-10-23-hello.md"), b"")

    def test_year_folder(self):
        status, text = self.send(path="_posts/2026/2026-10-23-hello.md")
        self.assertEqual(status, 200, text)
        self.assertNotEqual(self.remote_file("drafts", "_posts/2026/2026-10-23-hello.md"), b"")

    def test_quiet_client_let_go(self):
        topica_bridge.Handler.timeout = 1
        try:
            s = socket.create_connection(("127.0.0.1", self.server.server_address[1]), timeout=10)
            s.sendall(b"GET / HTTP/1.0\r\n")       # and never the blank line
            self.assertEqual(s.recv(100), b"")       # the bridge hangs up
            s.close()
        finally:
            topica_bridge.Handler.timeout = 30

    def test_too_large(self):
        self.server.config.max_bytes = 100
        status, text = self.send(body=b"x" * 101)
        self.assertEqual(status, 413)

    def test_private_by_default(self):
        self.assertIsNone(self.server.config.allow)
        self.assertEqual(self.send()[0], 200)          # loopback is private

    def test_address_not_admitted(self):
        self.server.config.allow = topica_bridge.Config({"TOPICA_BRIDGE_REPO": self.clone, "TOPICA_BRIDGE_KEY": KEY,
                                                         "TOPICA_BRIDGE_ALLOW": "10.0.0.0/8"}).allow
        self.assertEqual(self.send()[0], 403)

    def test_hugo_folder_and_unreachable_remote(self):
        self.assertEqual(self.send(path="content/posts/hello.md")[0], 200)
        git(self.clone, "remote", "set-url", "origin", os.path.join(self.tmp.name, "gone.git"))
        status, text = self.send(path="content/posts/other.md")
        self.assertEqual((status, text), (502, "The bridge couldn't push the post: see its log."))

    def test_config(self):
        with self.assertRaises(ValueError):
            topica_bridge.Config({"TOPICA_BRIDGE_REPO": self.clone, "TOPICA_BRIDGE_KEY": "short"})
        with self.assertRaises(ValueError):
            topica_bridge.Config({"TOPICA_BRIDGE_REPO": self.tmp.name, "TOPICA_BRIDGE_KEY": KEY})
        keyfile = os.path.join(self.tmp.name, "key")
        with open(keyfile, "w") as f:
            f.write(KEY + "\n")
        c = topica_bridge.Config({"TOPICA_BRIDGE_REPO": self.clone, "TOPICA_BRIDGE_KEY_FILE": keyfile})
        self.assertEqual(c.key, KEY.encode())


if __name__ == "__main__":
    unittest.main()
