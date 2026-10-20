# Self-healing pull requests

NEWOS uses two free layers to keep pull requests mergeable without asking
maintainers to babysit red checks. Neither layer requires a paid API key.

## What runs

`.github/workflows/pr-self-heal.yml` reacts to four situations:

| Situation | Trigger | What happens |
| --- | --- | --- |
| CI failed on a PR | `workflow_run` of **CI and publish** | Integrates the base branch, repairs whitespace, or asks CodeRabbit to fix the failure. |
| A PR conflicts | `pull_request_target` | Merges the base branch and resolves generated-asset conflicts. |
| `main` moved | `push` to `main`, every 6 hours, manual run | Scans same-repository PRs updated in the last 30 days and heals new conflicts. |
| A repair was pushed | `workflow_dispatch` of **CI and publish** | CI is dispatched explicitly because a push made with `GITHUB_TOKEN` does not start workflows. |

The existing `.github/workflows/auto-merge.yml` merges a PR once every check
and status for its head commit is green and the submodule pins are reachable.
It also merges the `workflow_dispatch` runs that the self-healer starts.

## Layer 1: deterministic repairs (no bot, no API)

The workflow checks out the trusted default branch separately from the pull
request. It never executes code from the PR; it only runs `git`, `gh` and
`python3` helpers copied from the default branch.

It can fix, without any AI:

- trailing whitespace and blank lines at end of file flagged by
  `git diff --check` (`scripts/ci/fix_whitespace.py`);
- a stale PR branch, by merging `main` into it;
- conflicts in generated assets (`web/assets/**` and
  `brand/app-icons/**`), keeping the PR side. Re-run `make brand` or
  `make web` if the source files also changed.

After a push the script dispatches **CI and publish** on the PR branch. That
run is what the auto-merge workflow observes.

## Layer 2: CodeRabbit (free OSS GitHub App)

Everything else is delegated to [CodeRabbit](https://github.com/apps/coderabbitai):

- `@coderabbitai fix-ci commit` for a failing build or test;
- `@coderabbitai resolve merge conflict` for source-code conflicts.

CodeRabbit is **free for public repositories** (its OSS plan includes the Team
features used here), runs as a GitHub App, and needs no API key, token or
subscription. `.coderabbit.yaml` enables the `fix_ci`, `resolve_merge_conflict`
and `autofix` finishing touches.

### One-time setup

1. Install the CodeRabbit GitHub App on `CRISTOP-bot/NEWOS`:
   <https://github.com/apps/coderabbitai>. Choose the free OSS plan.
2. **Recommended:** open CodeRabbit **Triage → Settings** and enable the
   **Auto-fix PRs** rule for this repository. It reacts to CI updates directly,
   repairs failing CI and review feedback for up to three rounds, and does not
   depend on PR comments. CodeRabbit Triage rules are available to OSS
   repositories with Team features; the rule editor will say so if the plan
   does not include it.
3. The workflow also posts the explicit command as `github-actions[bot]`
   (`@coderabbitai fix-ci commit` / `@coderabbitai resolve merge conflict`).
   This is the fallback when Triage is not enabled. If an installation ignores
   commands from bot accounts, step 2 is the reliable path.
4. Do not create an `OPENAI_API_KEY`, `ANTHROPIC_API_KEY` or similar secret.
   None is read by this workflow.

## Labels and limits

| Label | Meaning |
| --- | --- |
| `self-heal/ci-1` … `ci-3` | Number of automatic CI repair rounds used. |
| `self-heal/conflict-1` … `conflict-3` | Number of conflict repair rounds used. |
| `self-heal/ci-needs-human` | Three CI rounds failed, whitespace could not be fixed, or CI could not be dispatched. |
| `self-heal/conflict-needs-human` | The conflict was handed to CodeRabbit, whitespace could not be fixed, or three rounds failed. |
| `self-heal/needs-human` | The repair could not be pushed (for example, branch protection). |
| `self-heal/stop` | Disables self-healing **and** auto-merge for this PR. |

The first three "needs human" labels also stop auto-merge. Remove the label
(and push a new commit or re-run the workflow) to try again.

Self-healing is skipped for draft PRs, PRs authored by bots, and PRs whose
head branch lives in a fork. A fork's `GITHUB_TOKEN` is read-only, so the
workflow cannot push a repair there.

## Security notes

- `pull_request_target` runs the workflow from the base branch, not from the
  PR. The PR head is checked out only as data for `git merge`; no PR script,
  Makefile or test is executed.
- Pushes use the repository `GITHUB_TOKEN` with the narrow permissions
  declared in the workflow. No personal access token is stored.
- CodeRabbit's app has its own repository permissions and runs its repair in
  its own sandbox.
- Auto-merge never bypasses branch protection. If required reviews are
  enabled, a human still has to approve the PR.

## Troubleshooting

- **The bot does not answer.** Check that the CodeRabbit app is installed and
  that `@coderabbitai help` returns a command list on the PR. Also check
  `reviews.finishing_touches.fix_ci.enabled` in `.coderabbit.yaml`.
- **The bot declined a conflict.** CodeRabbit deliberately refuses ambiguous
  or security-critical conflicts. Resolve those manually.
- **The branch was updated but CI did not start.** The `self-heal/dispatch-failed` comment
  means the explicit dispatch failed. Run **CI and publish** manually from the
  Actions tab for the PR branch.
- **Nothing happens on a PR from a fork.** That is intentional; the workflow
  cannot push to a fork with `GITHUB_TOKEN`.
