# Infested voice clip importer

Run from the repository root:

```powershell
python tools/import_voice_clips.py
```

Requires Python with Tkinter (included in the normal Windows Python installer)
and ffmpeg. The window finds ffmpeg on PATH, or you can browse to its executable.

1. Open the generated ZIP or the extracted folder containing the transcript
   folders. Each top-level folder is a transcript, such as `Run away` or `Flee`.
   Audio inside its subfolders is included regardless of their names (for example
   `Run away/resident_evil_jill/take1.wav`). ZIPs should have transcript folders at
   their root; if a ZIP has a wrapper folder, extract it and open that wrapper.
   Select the game character for the entire batch using the dropdown. Import one
   character at a time; all discovered audio is assigned to the selected character.
2. Review the dropdown beside each transcript folder. Some exact names and common
   synonyms are suggested. Assign other transcripts manually, or leave `(skip)`.
   Assign `Run away`, `Retreat`, and `Flee` to `runaway` to combine their takes.
   The coverage display updates as you change mappings. **Check mappings** lists
   missing categories for the selected character; empty or skipped folders do
   not count. When converting with missing categories, a warning lets you proceed
   anyway or return to editing. Your current mappings stay in place either way.
3. Choose an empty destination **voices folder**, then click **Convert and copy**.
   The output is `<destination>/barry/runaway_1.wav`, `_2.wav`, etc.
   Copy these character folders into `<game executable directory>/infested/voices/`.
   You can also select that game's voices folder directly if the target clips do
   not already exist.

Each immediate subfolder of the selected source is one transcript row. Audio in
nested take folders is included. WAV, OGG, MP3, FLAC, M4A, AAC, AIFF, OPUS and WMA
are recognized. Non-audio files in ZIPs are ignored.

Folders and clips use natural filename order (`take2` before `take10`). All folders
assigned to the same character/category share consecutive take numbers starting
at 1. The game supports 16 takes per category; larger groups are rejected before
conversion. Jokes play in this order in the game.

Output is mono, 22,050 Hz, 16-bit PCM WAV. Originals are preserved, and existing
WAV or OGG takes are never overwritten. On a conversion error, the importer stops
and reports it; already converted clips remain in the destination. Move those
clips out or select a fresh destination before retrying.

Integration checks: `python tests/test_voice_importer.py`.
