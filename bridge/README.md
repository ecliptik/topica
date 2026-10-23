# topica-bridge

A classic Macintosh has no TLS, so Topica can't push to a git host itself.
Post › Publish sends the post in plain HTTP to this small server on your
LAN, which commits it to a **drafts branch** of your blog's repository and
pushes. It never touches any other branch: you merge the drafts branch, or
open a pull request from it, when you want the post on the site.

`topica_bridge.py` is Python 3 with the standard library only. Its
docstring lists every setting and reply.

## Setting it up

1. A clone of the blog's repository that only the bridge uses (its working
   tree is reset on every post), with a git identity for the commits and a
   way to push: a deploy key with write access to that one repository is
   the narrowest.

   ```sh
   git clone git@github.com:you/blog.git ~/topica-bridge/blog
   git -C ~/topica-bridge/blog config user.name "Topica"
   git -C ~/topica-bridge/blog config user.email "topica@example.com"
   ```

2. A key, shared by the bridge and Topica: at least 16 printable ASCII
   characters, in a file only the bridge's user can read.

   ```sh
   python3 -c "import secrets; print(secrets.token_urlsafe(24))" > ~/topica-bridge/key
   chmod 600 ~/topica-bridge/key
   ```

3. Run it on the LAN address the Mac can reach:

   ```sh
   TOPICA_BRIDGE_REPO=~/topica-bridge/blog \
   TOPICA_BRIDGE_KEY_FILE=~/topica-bridge/key \
   TOPICA_BRIDGE_BIND=0.0.0.0 \
   python3 topica_bridge.py
   ```

   `topica-bridge.service` is a systemd user unit to start from.

4. In Topica, Post › Publish asks for the bridge (`192.0.2.10:8023`: an
   IP address, since a classic Mac has no name lookup for it) and the key,
   and remembers both.

The drafts branch starts from `main` the first time (`TOPICA_BRIDGE_BASE`);
after that each post is a new commit on it. Posting the same text again
changes nothing.

## What it refuses

Any method but `POST`, any path but `/publish`, a source address off
`TOPICA_BRIDGE_ALLOW` (by default loopback, link-local and the private
ranges, but not the 100.64.0.0/10 shared range), a missing or wrong key, a
post over 512K, text that isn't UTF-8, and a path that isn't a Markdown
file right in `_posts` (named with its date, as Jekyll wants), `_drafts`
or `content/posts`.

## The trust boundary

The LAN. The key crosses it in clear, as everything from a classic Mac
does, and an address allow-list can be spoofed. What the key allows is
narrow: adding or changing a Markdown file on the drafts branch, where
every change is a commit you review before it reaches the site.

The log has the source, method, path, status and size: never the key or
the post.

## Tests

```sh
python3 -m unittest discover -s bridge
```

They make a bare repository in a temporary folder to stand for the blog's,
and talk to the bridge on loopback.
