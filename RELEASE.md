# Creating a Release

Two different GitHub Actions workflows exist:

| Workflow | File | When it runs | What it does |
|----------|------|-------------|--------------|
| **CI** | `ci.yml` | Every push/PR to `main` | Builds firmware + Calibre plugin, uploads as **temporary build artifacts** (visible in the Actions run, not on Releases page) |
| **Release** | `release.yml` | Only when you push a `v*` tag | Builds firmware + Calibre plugin, **creates a GitHub Release** with files attached |

The CI run you saw was the first one — it's just a sanity check.

## Step-by-step to create a real release

### 1. Make sure `main` has everything you want

```bash
git checkout main
git pull
```

### 2. Build releases without debug features

Releases must be built with `MR_ETA_DEBUG=0`. This disables the ETA debug
overlay and, by default, the persistent diagnostic log. The project default is
currently enabled for development, so verify the release build flags before
tagging:

```bash
pio run -e esp32c3-release
```

The GitHub Release workflow must also pass these flags when building the
firmware.

### 3. Choose the version number

Versions use the format `vMAJOR.MINOR.PATCH`. Look at the commits since the last
`v*` tag (`git log --oneline $(git describe --tags --abbrev=0 --match 'v[0-9]*')..HEAD`)
and pick the bump from the table. The highest applicable level wins.

| Bump | When | Example |
|------|------|---------|
| **PATCH** | Only fixes, performance work, docs or release-pipeline changes. No new user-facing features. | `2.2.1` → `2.2.2` |
| **MINOR** | At least one new user-facing feature or setting, with no forced migration. Resets PATCH to 0. | `2.2.1` → `2.3.0` |
| **MAJOR** | Breaking changes (e.g. a format change that forces re-conversion or re-indexing, or settings that reset), new hardware support, big new features, or architectural changes. Resets MINOR and PATCH to 0. | `2.3.0` → `3.0.0` |

Rules:

- Version numbers only go up. A published tag is never moved or reused; if a
  release is broken, fix it and tag the next PATCH.
- Do **not** use pre-release tags such as `-dev.N`. Any `v*` tag creates a real
  GitHub Release and becomes the version shown on the Settings screen. Testers can
  use the firmware artifact from the CI run on `main` instead.
- Docs-only or CI-only changes don't need a release on their own.

**Always ask the user to confirm the version before tagging.** The version is
calculated automatically from the scheme above, but it is only a proposal: state
the last tag, summarize what changed, name the calculated next version (and why),
and wait for the user to approve it or give a different one. Never create or push
a tag without that approval.

### 4. Tag and push

The tag **is** the version. There is no version file to bump: `tools/generate_version.py`
takes the base version from the latest `v*` tag and appends the commit count as the
build number. The Settings screen shows `<tag without v>.<build>` (e.g. `2.1.0.512`),
with `-dirty` appended if the firmware was built from uncommitted changes.

```bash
git tag v2.1.0
git push origin v2.1.0
```

The tag **must start with `v`** — that's what triggers the `Release` workflow.
Commits made after a release keep showing the old tag as base version
(`2.1.0.513`, …) until the next tag is created.

### 5. Watch it run

Go to **Actions** tab → look for the `Release` workflow. It takes ~3–4 minutes.

### 6. Find the release

Go to **Releases** page (right sidebar on the repo home, or `https://github.com/yourname/microreader-plus/releases`).

You'll see:
- `Microreader-TF-2.1.0.bin` — firmware to flash to your Xteink X4 (the version is taken from the tag)
- `Microreader-Calibre-Plugin-2.1.0.zip` — Calibre plugin (the version is taken from the tag)

## Troubleshooting

**"I pushed a tag but nothing happened"**
Make sure the tag starts with `v` (e.g. `v2.0.42`, not `2.0.42`).

**"Build fails on Actions but works on my machine"**
Check if you're on Windows and the CI runs Linux — most issues are path separator or SDK config related. If the CI fails, fix the problem and release it under the next PATCH version (see step 3). Only if the run failed before any GitHub Release was created may you delete and re-push that same tag.

**"Node.js 20 is deprecated" warning**
Harmless for now — GitHub Actions runners still support Node.js 20. This will be fixed when action maintainers release updates.
