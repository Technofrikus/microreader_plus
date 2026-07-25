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

### 2. (Optional) Bump the version

Edit `version.txt` if you want a new base version (otherwise it keeps the current value):

```bash
echo "2.1.0" > version.txt
git add version.txt
git commit -m "bump version to 2.1.0"
git push
```

### 3. Tag and push

```bash
git tag v2.1.0
git push origin v2.1.0
```

The tag **must start with `v`** — that's what triggers the `Release` workflow.

### 4. Watch it run

Go to **Actions** tab → look for the `Release` workflow. It takes ~3–4 minutes.

### 5. Find the release

Go to **Releases** page (right sidebar on the repo home, or `https://github.com/yourname/microreader-plus/releases`).

You'll see:
- `firmware.bin` — flash this to your Xteink X4
- `microreader.zip` — Calibre plugin

## Troubleshooting

**"I pushed a tag but nothing happened"**
Make sure the tag starts with `v` (e.g. `v2.0.42`, not `2.0.42`).

**"Build fails on Actions but works on my machine"**
Check if you're on Windows and the CI runs Linux — most issues are path separator or SDK config related. If the CI fails, fix and retag.

**"Node.js 20 is deprecated" warning**
Harmless for now — GitHub Actions runners still support Node.js 20. This will be fixed when action maintainers release updates.