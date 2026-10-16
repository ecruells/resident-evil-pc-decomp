"""Interactive voice pack importer. Run: python tools/import_voice_clips.py"""
from collections import defaultdict
from pathlib import Path, PurePosixPath
import queue
import re
import shutil
import subprocess
import tempfile
import threading
import wave
import zipfile
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

CHARACTERS = ('chris', 'jill', 'barry', 'rebecca', 'richard', 'enrico')
CATEGORIES = ('help', 'takethis', 'needammo', 'needhealth', 'followme',
              'watchout', 'taunt', 'joke', 'goodjob', 'guardme', 'leadtheway',
              'runaway', 'stayhere', 'hurt', 'dying', 'thanks', 'usethat',
              'werescrewed')
AUDIO = {'.wav', '.ogg', '.mp3', '.flac', '.m4a', '.aac', '.aiff', '.aif', '.opus', '.wma'}
SKIP = '(skip)'


def natural_key(value):
    return [int(s) if s.isdigit() else s.casefold()
            for s in re.split(r'(\d+)', str(value))]


def extract_audio(archive, destination):
    """Extract only audio, rejecting paths outside the temporary directory."""
    with zipfile.ZipFile(archive) as pack:
        for entry in pack.infolist():
            name = PurePosixPath(entry.filename.replace('\\', '/'))
            if entry.is_dir() or name.suffix.lower() not in AUDIO:
                continue
            if name.is_absolute() or '..' in name.parts or any(':' in p for p in name.parts):
                raise ValueError(f'Unsafe ZIP path: {entry.filename}')
            target = destination.joinpath(*name.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with pack.open(entry) as source, target.open('xb') as output:
                shutil.copyfileobj(source, output)


def scan(root):
    """Each transcript folder is a row; nested take folders belong to that row."""
    rows = []
    for folder in sorted(root.iterdir(), key=natural_key):
        if not folder.is_dir():
            continue
        clips = sorted((p for p in folder.rglob('*')
                        if p.is_file() and p.suffix.lower() in AUDIO), key=natural_key)
        rows.append((folder.relative_to(root), clips))
    return rows


def suggest(transcript):
    text = re.sub(r'[^a-z]', '', transcript.lower())
    aliases = {'retreat': 'runaway', 'flee': 'runaway', 'ineedammo': 'needammo',
               'ineedhealth': 'needhealth', 'imhurt': 'hurt', 'imdying': 'dying',
               'comeandgetme': 'taunt'}
    return text if text in CATEGORIES else aliases.get(text, SKIP)


def missing_categories(rows):
    covered = {}
    for character, category, clips in rows:
        covered.setdefault(character, set())
        if clips and category in CATEGORIES:
            covered[character].add(category)
    return {character: [category for category in CATEGORIES if category not in categories]
            for character, categories in sorted(covered.items())
            if len(categories) != len(CATEGORIES)}


def coverage_text(missing):
    return '\n'.join(f'{character}: {", ".join(categories)}'
                     for character, categories in missing.items())


def plan(rows, destination):
    groups = defaultdict(list)
    for character, category, clips in rows:
        if category == SKIP:
            continue
        if character not in CHARACTERS or category not in CATEGORIES:
            raise ValueError('Invalid character or category')
        groups[character, category].extend(clips)
    jobs = []
    for (character, category), clips in sorted(groups.items()):
        if len(clips) > 16:
            raise ValueError(f'{character}/{category} has {len(clips)} takes; the game supports 16.\n'
                             'Skip some transcript folders or reduce their takes before importing.')
        for number, clip in enumerate(clips, 1):
            target = destination / character / f'{category}_{number}.wav'
            if target.exists() or target.with_suffix('.ogg').exists():
                raise ValueError(f'Clip already exists: {target}\nChoose an empty destination folder.')
            jobs.append((clip, target))
    if not jobs:
        raise ValueError('Assign at least one folder containing audio to a category.')
    return jobs


def convert(source, target, ffmpeg):
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='voice-import-', dir=target.parent) as folder:
        pending = Path(folder) / 'clip.wav'
        result = subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-nostdin',
                                 '-n', '-i', str(source), '-map', '0:a:0', '-vn',
                                 '-ac', '1', '-ar', '22050', '-c:a', 'pcm_s16le',
                                 '-map_metadata', '-1', str(pending)], capture_output=True,
                                text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        if result.returncode:
            raise ValueError(f'{source.name}: {result.stderr.strip()}')
        with wave.open(str(pending), 'rb') as audio:
            if (audio.getnchannels(), audio.getframerate(), audio.getsampwidth(),
                    audio.getcomptype()) != (1, 22050, 2, 'NONE') or not audio.getnframes():
                raise ValueError(f'Invalid converted audio: {source.name}')
        try:
            output = target.open('xb')
        except FileExistsError:
            raise ValueError(f'Destination already exists: {target}')
        try:
            with output, pending.open('rb') as incoming:
                shutil.copyfileobj(incoming, output)
        except Exception:
            target.unlink()
            raise


class Importer:
    def __init__(self, window):
        self.window = window
        self.temp = None
        self.rows = []
        self.busy = False
        self.events = queue.Queue()
        window.title('Infested voice clip importer')
        window.geometry('920x650')
        controls = ttk.Frame(window, padding=10)
        controls.pack(fill='x')
        ttk.Button(controls, text='Open ZIP', command=lambda: self.open(True)).pack(side='left')
        ttk.Button(controls, text='Open folder', command=lambda: self.open(False)).pack(side='left', padx=8)
        self.source = ttk.Label(controls, text='Select a ZIP or a folder containing transcript folders.')
        self.source.pack(side='left')
        character_controls = ttk.Frame(window, padding=(10, 0))
        character_controls.pack(fill='x')
        ttk.Label(character_controls, text='Game character for this entire batch:').pack(side='left')
        self.character = tk.StringVar(value='jill')
        ttk.Combobox(character_controls, textvariable=self.character, values=CHARACTERS,
                     state='readonly', width=16).pack(side='left', padx=8)
        ttk.Label(window, text='Assign transcript folders to categories. Multiple folders can share a category; (skip) excludes a folder.',
                  padding=10).pack(anchor='w')
        area = ttk.Frame(window)
        area.pack(fill='both', expand=True, padx=10)
        canvas = tk.Canvas(area, highlightthickness=0)
        scrollbar = ttk.Scrollbar(area, orient='vertical', command=canvas.yview)
        canvas.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side='right', fill='y')
        canvas.pack(side='left', fill='both', expand=True)
        self.table = ttk.Frame(canvas)
        table_id = canvas.create_window((0, 0), window=self.table, anchor='nw')
        self.table.bind('<Configure>', lambda e: canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>', lambda e: canvas.itemconfigure(table_id, width=e.width))
        footer = ttk.Frame(window, padding=10)
        footer.pack(fill='x')
        self.coverage = tk.StringVar(value='Coverage: open a source to check its categories.')
        self.character.trace_add('write', self.update_coverage)
        ttk.Label(footer, textvariable=self.coverage, wraplength=780).grid(
            row=0, column=0, columnspan=3, sticky='w', pady=5)
        self.destination = tk.StringVar()
        self.ffmpeg = tk.StringVar(value=shutil.which('ffmpeg') or '')
        for row, (label, variable, callback) in enumerate([
                ('Destination voices folder', self.destination, self.choose_destination),
                ('ffmpeg executable', self.ffmpeg, self.choose_ffmpeg)], 1):
            ttk.Label(footer, text=label).grid(row=row, column=0, sticky='w')
            ttk.Entry(footer, textvariable=variable).grid(row=row, column=1, sticky='ew', padx=8, pady=3)
            ttk.Button(footer, text='Browse', command=callback).grid(row=row, column=2)
        footer.columnconfigure(1, weight=1)
        ttk.Button(footer, text='Check mappings', command=self.check_mappings).grid(row=3, column=0, pady=8)
        ttk.Button(footer, text='Convert and copy', command=self.start).grid(row=3, column=2, pady=8)
        self.status = tk.StringVar(value='Output: <destination>/<character>/<category>_1.wav, _2.wav, ...')
        ttk.Label(footer, textvariable=self.status, wraplength=700).grid(row=4, column=0, columnspan=3, sticky='w')
        window.protocol('WM_DELETE_WINDOW', self.close)
        window.after(100, self.poll)

    def choose_destination(self):
        if not self.busy:
            folder = filedialog.askdirectory(title='Choose destination voices folder')
            if folder:
                self.destination.set(folder)

    def choose_ffmpeg(self):
        if not self.busy:
            path = filedialog.askopenfilename(title='Select ffmpeg executable')
            if path:
                self.ffmpeg.set(path)

    def open(self, zipped):
        if self.busy:
            return
        path = (filedialog.askopenfilename(filetypes=[('ZIP archives', '*.zip')]) if zipped
                else filedialog.askdirectory())
        if not path:
            return
        temporary = None
        try:
            root = Path(path)
            if zipped:
                temporary = tempfile.TemporaryDirectory(prefix='infested-voices-')
                root = Path(temporary.name)
                extract_audio(path, root)
            rows = scan(root)
            if not rows:
                raise ValueError('No transcript folders found.\n'
                                 'Select the folder containing the transcript folders, or a ZIP with them at its root.')
        except Exception as error:
            if temporary:
                temporary.cleanup()
            messagebox.showerror('Could not scan source', str(error))
            return
        if self.temp:
            self.temp.cleanup()
        self.temp = temporary
        self.rows = []
        for child in self.table.winfo_children():
            child.destroy()
        for column, title in enumerate(('Transcript folder', 'Takes', 'Category')):
            ttk.Label(self.table, text=title).grid(row=0, column=column, sticky='w', padx=8, pady=5)
        self.table.columnconfigure(0, weight=1)
        for index, (folder, clips) in enumerate(rows, 1):
            category = tk.StringVar(value=suggest(folder.name))
            ttk.Label(self.table, text=str(folder), wraplength=470).grid(row=index, column=0, sticky='w', padx=8, pady=4)
            ttk.Label(self.table, text=str(len(clips))).grid(row=index, column=1, padx=8)
            ttk.Combobox(self.table, textvariable=category, values=(SKIP, *CATEGORIES),
                         state='readonly', width=18).grid(row=index, column=2, padx=8)
            self.rows.append((category, clips))
            category.trace_add('write', self.update_coverage)
        self.update_coverage()
        self.source.configure(text=path)
        self.status.set(f'{len(rows)} transcript folders scanned. Review suggested categories before importing.')

    def assignments(self):
        return [(self.character.get(), category.get(), clips) for category, clips in self.rows]

    def update_coverage(self, *_):
        missing = missing_categories(self.assignments())
        self.coverage.set('Missing categories:\n' + coverage_text(missing) if missing else
                          f'Coverage complete: all 18 categories have clips for {self.character.get()}.'
                          if self.rows else 'Coverage: open a source to check its categories.')

    def check_mappings(self):
        if not self.rows:
            messagebox.showwarning('Mappings', 'Open a source first.')
            return
        missing = missing_categories(self.assignments())
        if missing:
            messagebox.showwarning('Missing voice categories', coverage_text(missing) +
                                   '\n\nYou can still convert the categories you have mapped.')
        else:
            messagebox.showinfo('Mappings complete', f'All 18 categories have clips for {self.character.get()}.')

    def start(self):
        if self.busy:
            return
        try:
            ffmpeg = shutil.which(self.ffmpeg.get())
            if not ffmpeg:
                raise ValueError('Select ffmpeg or install it on PATH.')
            if not self.destination.get().strip():
                raise ValueError('Choose a destination voices folder.')
            destination = Path(self.destination.get()).resolve()
            assignments = self.assignments()
            jobs = plan(assignments, destination)
            # Prevent newly exported clips becoming part of the source on a later scan.
            for source, _ in jobs:
                if destination == source.parent or destination in source.parents:
                    raise ValueError('Destination must not contain source clips. Choose a separate output folder.')
        except Exception as error:
            messagebox.showerror('Cannot import', str(error))
            return
        missing = missing_categories(assignments)
        if missing and not messagebox.askyesno(
                'Missing voice categories', coverage_text(missing) +
                '\n\nContinue converting the mapped clips anyway?'):
            return
        self.busy = True
        self.status.set(f'Converting {len(jobs)} clips...')
        def work():
            count = 0
            try:
                for source, target in jobs:
                    convert(source, target, ffmpeg)
                    count += 1
                    self.events.put(('progress', f'Converted {count}/{len(jobs)}: {target.name}'))
                self.events.put(('done', f'Imported {count} clips into {destination}'))
            except Exception as error:
                self.events.put(('error', f'{count} clips converted before stopping.\n{error}\n'
                                 'Converted files remain in the destination; originals are preserved.'))
        threading.Thread(target=work, daemon=True).start()

    def poll(self):
        while not self.events.empty():
            kind, text = self.events.get_nowait()
            self.status.set(text)
            if kind in ('done', 'error'):
                self.busy = False
                if kind == 'error':
                    messagebox.showerror('Import stopped', text)
                else:
                    messagebox.showinfo('Import complete', text)
        self.window.after(100, self.poll)

    def close(self):
        if self.busy:
            messagebox.showinfo('Conversion running', 'Wait for conversion to finish before closing.')
            return
        if self.temp:
            self.temp.cleanup()
        self.window.destroy()


if __name__ == '__main__':
    window = tk.Tk()
    Importer(window)
    window.mainloop()
