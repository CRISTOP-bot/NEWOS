#!/usr/bin/env bash
# Self-heal shared helpers (sourced library, not executable on its own).
#
# This file holds the PR/git/gh primitives self-heal.sh drives: metadata
# refresh, labels and comments, checkout preparation, whitespace repair and
# the base-branch merge. The modes (conflict, ci) and the argument handling
# stay in self-heal.sh, which sources this file from the same trusted tree
# the workflow checks out (see .github/workflows/pr-self-heal.yml).
#
# The script never executes code from the pull request.  It is copied from the
# trusted default branch by the workflow and only runs git/gh commands against
# the PR checkout.
log() { printf 'self-heal: %s\n' "$*"; }
warn() { printf 'self-heal: warning: %s\n' "$*" >&2; }

refresh_meta() {
    local raw
    if ! raw="$(gh pr view "$PR_NUMBER" \
        --json state,isDraft,author,labels,baseRefName,headRefName,headRefOid,headRepository,mergeable,mergeStateStatus \
        2>/dev/null)"; then
        log "cannot read PR #$PR_NUMBER; skipping."
        exit 0
    fi
    meta="$raw"
    BASE_REF="$(jq -r '.baseRefName // empty' <<<"$meta")"
    PR_HEAD_REF="$(jq -r '.headRefName // empty' <<<"$meta")"
    if [ -z "$EXPECTED_HEAD_SHA" ]; then
        EXPECTED_HEAD_SHA="$(jq -r '.headRefOid // empty' <<<"$meta")"
    fi
    if [ -z "$BASE_REF" ] || [ -z "$PR_HEAD_REF" ] || [ -z "$EXPECTED_HEAD_SHA" ]; then
        log "incomplete PR metadata; skipping."
        exit 0
    fi
}

label_exists() {
    jq -e --arg name "$1" '.labels[]? | select(.name == $name)' <<<"$meta" >/dev/null
}

attempt_count() {
    local prefix="$1" max=0 name suffix
    refresh_meta
    while IFS= read -r name; do
        suffix="${name##*-}"
        if [[ "$suffix" =~ ^[0-9]+$ ]] && (( suffix > max )); then
            max="$suffix"
        fi
    done < <(jq -r --arg p "self-heal/${prefix}-" '.labels[]? | .name | select(startswith($p))' <<<"$meta")
    printf '%s\n' "$max"
}

ensure_label() {
    gh label create "$1" --color "$2" --description "$3" >/dev/null 2>&1 || true
}

add_label() {
    gh pr edit "$PR_NUMBER" --add-label "$1" >/dev/null 2>&1 \
        || warn "could not add label $1"
}

remove_label() {
    gh pr edit "$PR_NUMBER" --remove-label "$1" >/dev/null 2>&1 || true
}

comment_once() {
    local marker="$1" body="$2" existing
    existing="$(gh api --paginate \
        "repos/${GITHUB_REPOSITORY}/issues/${PR_NUMBER}/comments" \
        --jq '.[].body' 2>/dev/null || true)"
    if printf '%s\n' "$existing" | grep -Fq -- "$marker"; then
        log "comment already present."
        return 0
    fi
    if ! gh pr comment "$PR_NUMBER" --body "$body" >/dev/null; then
        warn "could not post a PR comment."
    fi
}

check_pr_state() {
    local state draft author head_repo
    state="$(jq -r '.state // empty' <<<"$meta")"
    draft="$(jq -r '.isDraft // false' <<<"$meta")"
    author="$(jq -r '.author.login // empty' <<<"$meta")"
    head_repo="$(jq -r '.headRepository.nameWithOwner // empty' <<<"$meta")"

    if [ "$state" != "OPEN" ]; then
        log "PR is not open; skipping."
        exit 0
    fi
    if [ "$draft" = "true" ]; then
        log "PR is a draft; skipping."
        exit 0
    fi
    case "$author" in
        *"[bot]")
            log "PR is authored by a bot; skipping."
            exit 0
            ;;
    esac
    if [ "${head_repo,,}" != "${GITHUB_REPOSITORY,,}" ]; then
        log "PR comes from a fork; this workflow only updates same-repository branches."
        exit 0
    fi
}

prepare_repo() {
    [ -d "$PR_DIR/.git" ] || {
        log "missing PR checkout at $PR_DIR."
        exit 0
    }
    cd "$PR_DIR"
    git config user.name "github-actions[bot]"
    git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
    if ! git fetch --no-tags origin "+refs/heads/${BASE_REF}:refs/remotes/origin/${BASE_REF}" >/dev/null 2>&1; then
        log "cannot fetch origin/${BASE_REF}; skipping."
        exit 0
    fi
    git rev-parse --verify "origin/${BASE_REF}" >/dev/null 2>&1 || {
        log "origin/${BASE_REF} is missing; skipping."
        exit 0
    }
}

verify_head() {
    local current
    current="$(git rev-parse HEAD)"
    if [ "$current" != "$EXPECTED_HEAD_SHA" ]; then
        log "checkout is stale (HEAD $current, expected $EXPECTED_HEAD_SHA); skipping."
        exit 0
    fi
}

verify_remote_head() {
    local remote
    remote="$(git ls-remote --heads origin "refs/heads/${PR_HEAD_REF}" 2>/dev/null | awk 'NR == 1 { print $1 }')"
    if [ "$remote" != "$EXPECTED_HEAD_SHA" ]; then
        log "branch ${PR_HEAD_REF} moved (remote ${remote:-none}); skipping."
        exit 0
    fi
}

dispatch_ci() {
    local _
    for _ in 1 2 3; do
        if gh workflow run ci-release.yml \
            --repo "$GITHUB_REPOSITORY" \
            --ref "$PR_HEAD_REF" >/dev/null 2>&1; then
            log "dispatched CI on ${PR_HEAD_REF}."
            return 0
        fi
        sleep 3
    done
    return 1
}

push_and_dispatch() {
    local kind="$1"
    verify_remote_head
    if ! git push --no-verify origin "HEAD:${PR_HEAD_REF}" >/dev/null 2>&1; then
        comment_once "<!-- newos-self-heal:push-failed:${kind}:${EXPECTED_HEAD_SHA} -->" \
            "### Self-heal: could not update the branch

The repair was generated locally, but the push to \`${PR_HEAD_REF}\` was
rejected. Branch protection or a concurrent update is the usual cause, so a
maintainer needs to push the change manually."
        add_label "self-heal/needs-human"
        return 1
    fi
    if ! dispatch_ci; then
        comment_once "<!-- newos-self-heal:dispatch-failed:${kind}:${EXPECTED_HEAD_SHA} -->" \
            "### Self-heal: branch updated, CI not dispatched

The branch \`${PR_HEAD_REF}\` was updated, but GitHub Actions could not be
started for the new commit. Re-run **CI and publish** manually for that branch."
        add_label "self-heal/${kind}-needs-human"
        return 1
    fi
    return 0
}

# Repair trailing whitespace / blank-line-at-EOF errors in the PR diff.
# Returns 0 when a follow-up commit was created, 1 when there was nothing to
# do, and 2 when errors remain (binary/large files or leftover conflict markers).
normalize_whitespace() {
    local committed=0

    if ! git diff --check "origin/${BASE_REF}...HEAD" >/tmp/self-heal-check.log 2>&1; then
        python3 "${TRUSTED_DIR}/scripts/ci/fix_whitespace.py" --base "origin/${BASE_REF}" || true
    fi

    if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
        git add -u
        if ! git diff --cached --quiet; then
            git commit --no-verify \
                -m "[autofix] normalize whitespace" \
                -m "Self-heal for ${WORKFLOW_NAME} run: ${RUN_URL:-unknown}"
            committed=1
        fi
    fi

    if ! git diff --check "origin/${BASE_REF}...HEAD" >/tmp/self-heal-check.log 2>&1; then
        warn "whitespace errors remain after the deterministic fix:"
        cat /tmp/self-heal-check.log >&2 || true
        return 2
    fi

    if (( committed == 1 )); then
        return 0
    fi
    return 1
}

# Merge the base branch into the PR.  Generated assets are resolved in favour
# of the PR; anything else is reported back to the caller.
attempt_merge() {
    local conflicts=() unresolved=() resolved=() f
    MERGE_CREATED=0
    CONFLICT_FILES=""
    CONFLICT_RESOLVED_FILES=""

    if git merge-base --is-ancestor "origin/${BASE_REF}" HEAD; then
        return 0
    fi

    if git merge --no-edit --no-ff "origin/${BASE_REF}" >/tmp/self-heal-merge.log 2>&1; then
        MERGE_CREATED=1
        return 0
    fi

    mapfile -t conflicts < <(git diff --name-only --diff-filter=U)
    if [ "${#conflicts[@]}" -eq 0 ]; then
        cat /tmp/self-heal-merge.log >&2 || true
        git merge --abort >/dev/null 2>&1 || true
        return 2
    fi

    for f in "${conflicts[@]}"; do
        case "$f" in
            web/assets/* | brand/app-icons/*)
                if git cat-file -e ":2:${f}" 2>/dev/null; then
                    if ! git checkout --ours -- "$f" >/dev/null 2>&1; then
                        unresolved+=("$f")
                        continue
                    fi
                else
                    git rm -f -- "$f" >/dev/null 2>&1 || true
                fi
                git add -- "$f" >/dev/null 2>&1 || true
                resolved+=("$f")
                ;;
            *)
                unresolved+=("$f")
                ;;
        esac
    done

    if [ "${#unresolved[@]}" -eq 0 ]; then
        git commit --no-verify --no-edit >/dev/null
        MERGE_CREATED=1
        CONFLICT_RESOLVED_FILES="$(printf '%s\n' "${resolved[@]}")"
        return 0
    fi

    CONFLICT_FILES="$(printf '%s\n' "${unresolved[@]}")"
    git merge --abort >/dev/null 2>&1 || true
    return 1
}

cleanup_conflict_labels() {
    local i
    for i in 1 2 3 4 5; do
        remove_label "self-heal/conflict-${i}"
    done
    remove_label "self-heal/conflict-needs-human"
}
