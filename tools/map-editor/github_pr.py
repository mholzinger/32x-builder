"""GitHub OAuth + fork/branch/PR creation for the map editor (pipeline Phase 3).

The editor's "Sign in with GitHub" uses a standard OAuth App authorization-code
flow; the resulting USER token (public_repo scope) lives only in the user's own
signed session cookie. /submit_pr then acts AS THE USER via the REST API:

    fork the game repo -> (sync fork) -> branch map-<slug> -> PUT the .map file
    -> open the PR against upstream main

so every community map arrives as a normal GitHub PR authored by its creator,
gated by the same CI lint+build as any hand-made PR. No server-held tokens, no
bot identity, nothing to leak: the fly app only ever proxies the user's own
credential back to GitHub over TLS.

Uses urllib (stdlib) — no new image dependencies.
"""
import json
import time
import urllib.error
import urllib.parse
import urllib.request

API = "https://api.github.com"
UA = {"User-Agent": "backrooms-32x-map-editor", "Accept": "application/vnd.github+json"}


class GitHubError(Exception):
    def __init__(self, msg, status=0):
        super().__init__(msg)
        self.status = status


def _req(method, url, token=None, body=None, headers=None, timeout=15):
    h = dict(UA)
    if token:
        h["Authorization"] = "Bearer " + token
    if headers:
        h.update(headers)
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        h["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=h, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            text = r.read().decode()
            return r.status, (json.loads(text) if text else {})
    except urllib.error.HTTPError as e:
        text = e.read().decode()
        try:
            payload = json.loads(text)
        except ValueError:
            payload = {"message": text[:200]}
        return e.code, payload
    except (urllib.error.URLError, TimeoutError) as e:
        raise GitHubError("GitHub unreachable: %s" % e)


# ---------------- OAuth (authorization-code exchange) ----------------

def authorize_url(client_id, redirect_uri, state):
    return ("https://github.com/login/oauth/authorize?" +
            urllib.parse.urlencode({
                "client_id": client_id, "redirect_uri": redirect_uri,
                "scope": "public_repo", "state": state}))


def exchange_code(client_id, client_secret, code):
    """OAuth code -> user access token."""
    status, j = _req("POST", "https://github.com/login/oauth/access_token",
                     body={"client_id": client_id, "client_secret": client_secret,
                           "code": code},
                     headers={"Accept": "application/json"})
    if status != 200 or "access_token" not in j:
        raise GitHubError("token exchange failed: %s" % j.get("error_description", j), status)
    return j["access_token"]


def whoami(token):
    status, j = _req("GET", API + "/user", token)
    if status != 200:
        raise GitHubError("user lookup failed (%s)" % status, status)
    return j["login"]


# ---------------- the contributor's own fork ----------------
#
# A fork is not just plumbing on the way to a PR — it is the contributor's OWN
# copy of the game, and that is where map and sprite work should live. In their
# fork they own the whole budget: the community CRAM arena, the sprite kind
# numbering, the ROM. They can split assets however they like without competing
# with anyone else's uploads, their fork's CI cuts them a ROM on every push, and
# a PR upstream stays available for anything they want in the main game.

def fork_name(login, upstream):
    up_owner, repo_name = upstream.split("/")
    return upstream if login == up_owner else "%s/%s" % (login, repo_name)


def fork_status(token, login, upstream):
    """{"fork", "exists", "is_upstream", "html_url", "default_branch"} — what
    the editor shows so a contributor can see they have their own copy."""
    fork = fork_name(login, upstream)
    status, j = _req("GET", API + "/repos/%s" % fork, token)
    return {"fork": fork,
            "exists": status == 200,
            "is_upstream": fork == upstream,
            "html_url": j.get("html_url") if status == 200 else None,
            "default_branch": j.get("default_branch") or "main"}


def ensure_fork(token, login, upstream, base_branch, wait=20):
    """Create the contributor's fork if it doesn't exist and wait for GitHub to
    finish copying it (forking is async). Idempotent: an existing fork is
    fast-forwarded to upstream instead."""
    fork = fork_name(login, upstream)
    if fork == upstream:
        return fork
    status, _j = _req("GET", API + "/repos/%s" % fork, token)
    if status != 200:
        _req("POST", API + "/repos/%s/forks" % upstream, token)   # 202 either way
        for _ in range(wait):
            status, _j = _req("GET", API + "/repos/%s" % fork, token)
            if status == 200:
                break
            time.sleep(1)
        else:
            raise GitHubError("fork of %s not ready — try again in a minute" % upstream)
    _req("POST", API + "/repos/%s/merge-upstream" % fork, token,
         body={"branch": base_branch})       # best-effort catch-up
    return fork


def get_file(token, repo, path, ref="main"):
    """Fetch one text file from a repo, or None if it isn't there. Used to read
    a contributor's OWN registry and textures so the editor can show their
    assets — their fork is the source of truth for what their ROM will build."""
    import base64
    status, j = _req("GET", API + "/repos/%s/contents/%s?ref=%s"
                     % (repo, urllib.parse.quote(path), ref), token)
    if status != 200 or "content" not in j:
        return None
    try:
        return base64.b64decode(j["content"]).decode()
    except Exception:
        return None


def commit_to_fork(token, login, upstream, base_branch, files, message):
    """Commit files straight onto the contributor's OWN fork (its default
    branch) — no PR, no review, their repo. Their fork's CI then builds a ROM
    with their work in it. Returns {"fork", "commit_url", "repo_url"}."""
    for _p, text in files:
        if not (text and text.strip()):
            raise GitHubError("refusing to commit an empty file — reload the "
                              "editor and try again.")
    fork = ensure_fork(token, login, upstream, base_branch)
    import base64
    last = None
    for path, text in files:
        put = {"message": message, "branch": base_branch,
               "content": base64.b64encode(text.encode()).decode()}
        status, j = _req("GET", API + "/repos/%s/contents/%s?ref=%s"
                         % (fork, path, base_branch), token)
        if status == 200 and "sha" in j:
            put["sha"] = j["sha"]           # update in place on a re-save
        status, j = _req("PUT", API + "/repos/%s/contents/%s" % (fork, path),
                         token, body=put)
        if status not in (200, 201):
            raise GitHubError("commit failed (%s): %s"
                              % (path, j.get("message", status)), status)
        last = j
    return {"fork": fork,
            "commit_url": (last or {}).get("commit", {}).get("html_url"),
            "repo_url": "https://github.com/%s" % fork,
            "actions_url": "https://github.com/%s/actions" % fork}


# ---------------- fork -> branch -> file -> PR ----------------

def open_map_pr(token, login, upstream, base_branch, path, text, title, body):
    """Single-file wrapper (the map flow) over open_files_pr."""
    slug = path.rsplit("/", 1)[-1].rsplit(".", 1)[0]
    return open_files_pr(token, login, upstream, base_branch,
                         [(path, text)], title, body, "map-" + slug)


def open_files_pr(token, login, upstream, base_branch, files, title, body, branch):
    """The whole submission dance for ONE OR MORE text files (a map, or a
    community sprite's tex.h + registry.json), idempotent where possible.
    Returns {"url", "number", "existing": bool}."""
    for _p, text in files:
        if not (text and text.strip()):
            raise GitHubError("refusing to commit an empty file — the editor "
                              "sent no content. Reload the editor and resubmit.")
    owner_repo = upstream.split("/")
    if len(owner_repo) != 2:
        raise GitHubError("bad upstream repo %r" % upstream)
    up_owner, repo_name = owner_repo
    fork = "%s/%s" % (login, repo_name)

    if login == up_owner:
        fork = upstream          # the maintainer submitting to their own repo

    # 1. Ensure the fork exists (async on GitHub's side — poll until ready).
    if fork != upstream:
        _req("POST", API + "/repos/%s/forks" % upstream, token)   # 202 either way
        for _ in range(20):
            status, _j = _req("GET", API + "/repos/%s" % fork, token)
            if status == 200:
                break
            time.sleep(1)
        else:
            raise GitHubError("fork of %s not ready — try again in a minute" % upstream)
        # Best-effort: fast-forward the fork's default branch so the PR diff is
        # clean. Ignorable failures (conflict/pre-synced) don't block the PR.
        _req("POST", API + "/repos/%s/merge-upstream" % fork, token,
             body={"branch": base_branch})

    # 2. Branch on the fork at its current base tip (reuse if it exists).
    status, j = _req("GET", API + "/repos/%s/git/ref/heads/%s" % (fork, base_branch), token)
    if status != 200:
        raise GitHubError("can't read %s branch of %s (%s)" % (base_branch, fork, status), status)
    base_sha = j["object"]["sha"]
    status, j = _req("POST", API + "/repos/%s/git/refs" % fork, token,
                     body={"ref": "refs/heads/" + branch, "sha": base_sha})
    if status not in (201, 422):       # 422 = branch already exists (resubmit)
        raise GitHubError("branch create failed: %s" % j.get("message", status), status)

    # 3. Create/update each file on that branch (contents API needs the
    #    existing blob sha for updates — resubmits amend the same PR).
    import base64
    for path, text in files:
        put = {"message": "%s: %s\n\nSubmitted from the hosted editor." % (branch, path),
               "content": base64.b64encode(text.encode()).decode(),
               "branch": branch}
        status, j = _req("GET", API + "/repos/%s/contents/%s?ref=%s" % (fork, path, branch), token)
        if status == 200 and "sha" in j:
            put["sha"] = j["sha"]
        status, j = _req("PUT", API + "/repos/%s/contents/%s" % (fork, path), token, body=put)
        if status not in (200, 201):
            raise GitHubError("file commit failed (%s): %s" % (path, j.get("message", status)), status)

    # 4. The PR itself (or find the one already open for this branch).
    head = branch if fork == upstream else "%s:%s" % (login, branch)
    status, j = _req("POST", API + "/repos/%s/pulls" % upstream, token,
                     body={"title": title, "head": head, "base": base_branch, "body": body})
    if status == 201:
        return {"url": j["html_url"], "number": j["number"], "existing": False}
    if status == 422:                   # PR already open for this head — find it
        q = urllib.parse.urlencode({"head": head if ":" in head else "%s:%s" % (up_owner, head),
                                    "state": "open"})
        status2, prs = _req("GET", API + "/repos/%s/pulls?%s" % (upstream, q), token)
        if status2 == 200 and prs:
            return {"url": prs[0]["html_url"], "number": prs[0]["number"], "existing": True}
        raise GitHubError("PR exists but couldn't be found: %s" % j.get("message", ""), status)
    raise GitHubError("PR create failed: %s" % j.get("message", status), status)
