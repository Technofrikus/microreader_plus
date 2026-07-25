# Creating a Release

This project uses **GitHub Actions** to build and publish releases automatically.
You only need to tag a commit — the workflow does the rest.

## Step-by-step

### 1. Make sure `main` is in the state you want

```bash
git checkout main
git pull
```

### 2. (Optional) Bump the version number

The version shown in the firmware comes from `version.txt` plus the git commit
count. If you want a new base version, edit `version.txt`:

```bash
echo "2.1.0" > version.txt
git add version.txt
git commit -m "bump version to 2.1.0"
git push
```

If you skip this step, the release will use whatever is currently in
`version.txt` (e.g. `2.0-dev.412`).

### 3. Tag the commit and push

```bash
git tag v2.1.0
git push origin v2.1.0
```

The tag **must start with `v`** — that's what triggers the release workflow.

### 4. Wait for the workflow to finish

Go to **Actions** tab in the repo → look for the running `Release` workflow.
It takes about 3–4 minutes.

### 5. Find the release

Once done, go to the **Releases** page — the new release will be there with:

- `firmware.bin` — flash this to your Xteink X4
- `microreader.zip` — Calibre plugin (install via Preferences → Plugins)

## What the workflow does

1. Builds the ESP32 firmware (`pio run`)
2. Packages the Calibre plugin (`python build.py`)
3. Creates a GitHub Release with auto-generated notes from recent commits
4. Attaches both files to the release

## What it does NOT do

- Build desktop binaries (run `cmake -S platforms/desktop -B build` locally)
- Run tests (run `cd test && cmake -B build2 && cmake --build build2` locally)
- Flash the device (use esptool or the Crosspoint web flasher)
