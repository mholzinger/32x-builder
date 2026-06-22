#!/usr/bin/env bash
# Deploy the Backrooms 32X map editor to fly.io (app: backrooms-32x-project).
# Run from the repo root. Needs flyctl, authenticated (fly auth login).
#   ./deploy-to-flyio.sh            # normal deploy
#   ./deploy-to-flyio.sh --no-cache # clean rebuild
set -e
cd "$(dirname "$0")"
exec fly deploy "$@"
