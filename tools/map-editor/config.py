"""Env-var config for the Backrooms map editor (borrowed pattern from the
intellivision-overlay-editor)."""
import os

HOST = os.environ.get("MAP_HOST", "127.0.0.1")
PORT = int(os.environ.get("MAP_PORT", "5050"))
DEBUG = os.environ.get("MAP_DEBUG", "0") == "1"

# Repo root holds maps/, registry.json and tools/mapfmt.py. Defaults to three
# levels up from this file (tools/map-editor/config.py -> repo root); override
# with MAP_REPO_ROOT (the Docker compose sets it to the mounted /repo).
REPO_ROOT = os.environ.get(
    "MAP_REPO_ROOT",
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
MAPS_DIR = os.path.join(REPO_ROOT, "maps")
REGISTRY = os.path.join(REPO_ROOT, "registry.json")
TOOLS_DIR = os.path.join(REPO_ROOT, "tools")
