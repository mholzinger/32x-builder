#!/bin/bash
# Build + run the Backrooms 32X map editor in Docker from a CLEAN image
# (no cache), then smoke-test the endpoints. Run from the repo root:
#     ./test-docker-nocache.sh
# Leaves the container running at http://localhost:5050.
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
EDITOR_DIR="$ROOT/tools/map-editor"
PORT=5050
CONTAINER=backrooms-map-editor

echo "🐳 Backrooms 32X Map Editor — Docker (CLEAN, NO CACHE)"
echo "======================================================"

# docker compose v2 ("docker compose") vs v1 ("docker-compose").
if docker compose version >/dev/null 2>&1; then
    DC="docker compose"
elif command -v docker-compose >/dev/null 2>&1; then
    DC="docker-compose"
else
    echo "❌ Neither 'docker compose' nor 'docker-compose' found. Install Docker."
    exit 1
fi
echo "🔧 Using: $DC"

cd "$EDITOR_DIR"   # compose file lives here; its volume paths are relative to it

echo ""
echo "🧹 Tearing down existing container, networks, volumes..."
$DC down --volumes --remove-orphans 2>/dev/null || true

echo ""
echo "🗑️  Pruning dangling images..."
docker image prune -f >/dev/null

echo ""
echo "📦 Building image (NO CACHE)..."
$DC build --no-cache --pull

echo ""
echo "🚀 Starting container (fresh)..."
$DC up -d --force-recreate

echo ""
echo "⏳ Waiting for the app to come up..."
for i in $(seq 1 20); do
    if curl -s -o /dev/null "http://localhost:$PORT/"; then break; fi
    sleep 1
done

echo ""
echo "🧪 Testing endpoints..."
fail=0
check_200 () {
    code=$(curl -s -o /dev/null -w "%{http_code}" "$1")
    if [ "$code" = "200" ]; then echo "✅ $2: OK"; else echo "❌ $2: HTTP $code"; fail=1; fi
}
check_200 "http://localhost:$PORT/"                       "Main page"
check_200 "http://localhost:$PORT/static/css/style.css"   "CSS (style.css)"
check_200 "http://localhost:$PORT/static/js/app.js"       "JS (app.js)"
check_200 "http://localhost:$PORT/registry"               "API (registry)"
check_200 "http://localhost:$PORT/maps"                   "API (maps list)"

echo ""
echo "🗺️  Functional checks (repo mount + .map parse)..."
# The dogfood map must load through the /repo mount and parse via mapfmt.
if curl -s "http://localhost:$PORT/maps/backrooms" | grep -q '"BACKROOMS"'; then
    echo "✅ Load maps/backrooms.map: OK (mount + mapfmt parse)"
else
    echo "❌ Load maps/backrooms.map: FAILED"; fail=1
fi
# Save round-trip: server must accept a model and write a parseable .map.
if curl -s -X POST -H 'Content-Type: application/json' \
        --data '{"name":"DOCKERTEST","w":4,"h":4,"spawn":{"x":2,"y":2,"facing":"N"},"grid":["####","#..#","#..#","####"],"crawls":[],"partitions":[],"decals":[],"options":{"place_outlets":0,"place_exit_door":0,"lobby_ceiling":0}}' \
        "http://localhost:$PORT/maps/dockertest" | grep -q '"ok": *true'; then
    echo "✅ Save round-trip (POST -> .map): OK"
    # clean up the scratch map the test wrote into the repo
    rm -f "$ROOT/maps/dockertest.map"
else
    echo "❌ Save round-trip: FAILED"; fail=1
fi

echo ""
echo "======================================================"
if [ "$fail" = "0" ]; then
    echo "🎉 All checks passed."
else
    echo "⚠️  Some checks failed — see above and: $DC logs"
fi
echo ""
echo "📝 Next:"
echo "   • Open:   http://localhost:$PORT"
echo "   • Logs:   (cd tools/map-editor && $DC logs -f)"
echo "   • Stop:   (cd tools/map-editor && $DC down)"
echo "======================================================"
exit $fail
