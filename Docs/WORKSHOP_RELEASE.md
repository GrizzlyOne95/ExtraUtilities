# Workshop Release Notes

This file records repository-side publication details for the Extra Utilities Steam Workshop item.

## Publication sources

The Workshop uploader consumes:

- everything under `Workshop/` - `squish.py` walks that folder recursively, so a file
  added there ships without any change here. It currently holds `ExtraUtilities.ini`,
  `RequireFix.lua`, `monkey.jpg`, and the weather set (`exu_weather.lua`,
  `exu_weather.particle`, `exu_weather.material`, the Ogre-particle shaders/program,
  and the four `exu_*.png` textures).
- everything under `Definitions/` and `Release/`
- `workshop_description.txt`
- `workshop_changenote.txt`
- the packaged `Build/` directory produced for publication

### Packaged layout

`squish.py` rebuilds `Build/` from scratch with a declared layout:

- `Release/exu.dll` and `Release/exu.pdb` go to `Bin/` (the build fails if either is missing);
- `Workshop/` is copied to the `Build/` root, keeping any subfolders;
- `Definitions/` is copied to `Build/Definitions/`;
- `RequireFix.lua`, `exu_weather.lua` and `ExtraUtils.lua` are moved to `Scripts/`, and
  `monkey.jpg` to `Assets/`.

Two sources that would land on the same path (compared case-insensitively) fail the build
instead of silently overwriting each other. The script resolves paths from its own location,
so it can run from any directory; pass `--no-pause` when scripting it.

Runtime Lua must end up in `Scripts/`. That folder is already on the default Lua path -
`RequireFix.lua` lives there and missions require it by bare name before any path setup
runs. A module left at the `Build/` root resolves only after an explicit
`RequireFix.Initialize(<workshop id>)`, which adds `<mod>\?.lua`.

`Definitions/*.lua` are editor metadata (`--- @meta exu`) rather than runtime modules;
`ExtraUtils.lua` raises an error if it is ever required. They are shipped for tooling
only and their position is not a precedent for a real module. The others sit in
`Definitions/` rather than at the mod root, so generic names such as `Storage.lua` cannot
shadow a mission's own module once `RequireFix.Initialize` adds the mod root to the Lua path.

The release DLL is linked with `/PDBALTPATH:%_PDB%`, so `exu.dll` records only `exu.pdb`
rather than the build machine's absolute path.

`upload_workshop.py` generates the local `workshop.vdf` manifest and invokes SteamCMD using credentials/path settings from `.env`. The generated manifest and `.env` are intentionally ignored by Git.

## Versioning

Do not put a release version in the static `customtags` list. The DLL/API release identity is maintained in source and must stay synchronized across:

- `src/About.h`
- `include/ExtraUtils.h`
- `Definitions/ExtraUtils.lua`

Release tags are validated against `src/About.h` by CI.

## Description and changenote

Review `workshop_description.txt` before publication so installation guidance, repository links, compatibility statements, and credits still match the current project.

Update `workshop_changenote.txt` for each Workshop publication; it is intentionally a publication input rather than a historical changelog.

## Thumbnail uploader quirk

SteamCMD has historically failed to update this Workshop item's thumbnail reliably even when the preview file is present in the generated manifest. If a thumbnail change does not stick through SteamCMD, use the stock Battlezone/Steam Workshop uploader for the thumbnail update.

## Safety checks before upload

1. Build and validate the exact EXU DLL intended for publication.
2. Confirm the Workshop description and changenote are current.
3. Confirm `Workshop/ExtraUtilities.ini` contains no stale version-specific tag.
4. Package the Workshop payload and inspect `Build/` before upload.
5. Never commit `.env`, `workshop.vdf`, Steam credentials, local player state, or generated build output.
