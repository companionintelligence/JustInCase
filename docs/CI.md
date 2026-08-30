# Running CI for this repo

Automatic workflow triggers in this repository are **gated**: pushing does not
start a GitHub-hosted run any more. Checks run on local hardware, and the
workflows that remain are started deliberately.

Why: GitHub Actions was costing the org $563/month, almost all of it hosted
compute for checks that run just as well on hardware we already own.

Starting a gated workflow needs the [`gh` CLI](https://cli.github.com) and push
access. `--ref` matters - the workflow file is read from that branch, so a
workflow that only exists on your feature branch must be dispatched against it.

Full cross-repo runbook: `CI-Local-CICD/RUNBOOK.md` (regenerate with
`ci-local docs`). This file is generated - do not hand-edit.

---

Default branch: `main`

### Test locally

_No local checks detected._

### Run a check workflow on GitHub

**CI** — `ci.yml`

```bash
gh workflow run ci.yml --repo companionintelligence/JustInCase --ref main
```

**Style canon lint** — `style-canon-lint.yml`

```bash
gh workflow run style-canon-lint.yml --repo companionintelligence/JustInCase --ref main
```

### Cut a release

**Publish container** — `docker-publish.yml`

```bash
gh workflow run docker-publish.yml --repo companionintelligence/JustInCase --ref main
```
