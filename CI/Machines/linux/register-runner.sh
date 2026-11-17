#!/bin/bash
# register-runner.sh UNREAL_VERSION TOKEN: register this machine's runner for one Unreal version with the
# tempo-sim org, in runner group tempo-ci, and start it. Run from an admin account.
#
# TOKEN is a registration token: org Settings > Actions > Runners > New runner shows one, or
#   gh api -X POST orgs/tempo-sim/actions/runners/registration-token -q .token
# It expires after an hour and is only used here.
#
# One runner per Unreal version, because a job's build tree is fixed per version and not locked: the
# native workflow adds tempo-ue-<version> to every job's labels, so only this runner takes that
# version's jobs. Re-running for a version replaces its registration.
set -eu
[ $# -eq 2 ] || { sed -n '2,12p' "$0" >&2; exit 2; }
VERSION=$1 TOKEN=$2
HERE=$(cd "$(dirname "$0")" && pwd)
CI_USER=${TEMPO_CI_USER:-ci}
CI_HOME=/home/$CI_USER
ORG=${TEMPO_CI_ORG:-tempo-sim}
GROUP=${TEMPO_CI_RUNNER_GROUP:-tempo-ci}
as_ci() { sudo -u "$CI_USER" -H "$@"; }
NAME=${TEMPO_CI_MACHINE_NAME:-$(as_ci bash -c '. ~/tempo-ci/machine.env 2>/dev/null; echo "${TEMPO_CI_MACHINE_NAME:-$(hostname -s)}"')}-$VERSION
DIR=$CI_HOME/actions-runner-$VERSION

if ! as_ci test -x "$DIR/config.sh"; then
  TAG=$(curl -fsSL https://api.github.com/repos/actions/runner/releases/latest | python3 -c 'import json, sys; print(json.load(sys.stdin)["tag_name"])')
  echo "Installing runner ${TAG} into $DIR"
  as_ci mkdir -p "$DIR"
  curl -fsSL "https://github.com/actions/runner/releases/download/$TAG/actions-runner-linux-x64-${TAG#v}.tar.gz" \
    | as_ci tar -xz -C "$DIR"
fi

UNIT=$(as_ci cat "$DIR/.service" 2>/dev/null || true)
[ -z "$UNIT" ] || sudo systemctl stop "$UNIT" 2>/dev/null || true
# config.sh refuses a directory that is already configured, and removing that registration needs a
# different token. --replace takes over the name on GitHub's side instead, so forget it locally.
as_ci rm -f "$DIR/.runner" "$DIR/.credentials" "$DIR/.credentials_rsaparams"
as_ci bash -c "cd '$DIR' && ./config.sh --unattended --replace --url 'https://github.com/$ORG' --token '$TOKEN' \
  --name '$NAME' --runnergroup '$GROUP' --labels 'tempo-ci,tempo-ue-$VERSION' --work _work"
# config.sh leaves its caller's PATH behind and no service; bootstrap writes both.
"$HERE/bootstrap.sh" runners
