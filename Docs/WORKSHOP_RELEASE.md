# Workshop Release Notes

This file records repository-side publication details for the Extra Utilities Steam Workshop item.

## Publication sources

The Workshop uploader consumes:

- `Workshop/ExtraUtilities.ini`
- `Workshop/RequireFix.lua`
- `Workshop/monkey.jpg`
- `workshop_description.txt`
- `workshop_changenote.txt`
- the packaged `Build/` directory produced for publication

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
