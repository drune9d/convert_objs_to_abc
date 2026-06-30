#!/usr/bin/env python3
import os
import queue
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk


IS_FROZEN = bool(getattr(sys, "frozen", False))
RESOURCE_DIR = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
APP_DIR = Path(sys.executable).resolve().parent if IS_FROZEN else RESOURCE_DIR
BINARY_PATH = RESOURCE_DIR / "bin" / "Objs2Abc.exe"
BUILD_SCRIPT = RESOURCE_DIR / "build.ps1"
SAMPLE_DIR = RESOURCE_DIR / "head-poses"
DEFAULT_OUTPUT_DIR = (
    Path.home() / "Documents" / "OBJ Sequence to Alembic Output"
    if IS_FROZEN
    else APP_DIR / "output"
)


def obj_files(folder):
    try:
        return sorted(
            [p for p in Path(folder).expanduser().iterdir() if p.is_file() and p.suffix.lower() == ".obj"],
            key=lambda p: p.name.lower(),
        )
    except OSError:
        return []


def inspect_obj(path):
    counts = {"v": 0, "vt": 0, "vn": 0, "f": 0}
    try:
        with Path(path).open("r", encoding="utf-8", errors="ignore") as handle:
            for line in handle:
                if line.startswith("v "):
                    counts["v"] += 1
                elif line.startswith("vt "):
                    counts["vt"] += 1
                elif line.startswith("vn "):
                    counts["vn"] += 1
                elif line.startswith("f "):
                    counts["f"] += 1
    except OSError:
        pass
    return counts


def format_command(command):
    return subprocess.list2cmdline([str(part) for part in command])


def converter_ready():
    return BINARY_PATH.is_file()


class ObjToAlembicApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("OBJ Sequence -> Alembic")
        self.minsize(720, 560)

        self.log_queue = queue.Queue()
        self.process = None
        self.current_job = None
        self.last_output_path = None
        self.total_frames = 0

        self.input_dir = tk.StringVar()
        self.output_dir = tk.StringVar()
        self.output_filename = tk.StringVar(value="output.abc")
        self.framerate = tk.StringVar(value="24")
        self.node_name = tk.StringVar(value="Mesh")
        self.status_text = tk.StringVar()
        self.sequence_text = tk.StringVar(value="Choose an input folder to inspect the sequence.")
        self.output_preview = tk.StringVar(value="Output path will appear here.")

        for variable in (self.input_dir, self.output_dir, self.output_filename):
            variable.trace_add("write", lambda *_: self._refresh_preview())

        self._configure_style()
        self._build_ui()
        self._refresh_converter_status()
        self._refresh_preview()
        self.after(100, self._drain_log_queue)

    def _configure_style(self):
        style = ttk.Style(self)
        if "clam" in style.theme_names():
            style.theme_use("clam")
        style.configure("Title.TLabel", font=("Segoe UI", 20, "bold"))
        style.configure("Subtitle.TLabel", foreground="#475569")
        style.configure("Section.TLabel", font=("Segoe UI", 12, "bold"))
        style.configure("Primary.TButton", padding=(14, 8))
        style.configure("Tool.TButton", padding=(10, 6))

    def _build_ui(self):
        self.columnconfigure(0, weight=1)
        self.rowconfigure(1, weight=1)

        header = ttk.Frame(self, padding=(18, 16, 18, 8))
        header.grid(row=0, column=0, sticky="ew")
        header.columnconfigure(0, weight=1)

        ttk.Label(header, text="OBJ Sequence -> Alembic", style="Title.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(
            header,
            text="Convert a folder of OBJ frames into an Alembic mesh cache.",
            style="Subtitle.TLabel",
        ).grid(row=1, column=0, sticky="w", pady=(2, 0))

        self.status_badge = tk.Label(header, textvariable=self.status_text, padx=10, pady=4)
        self.status_badge.grid(row=0, column=1, rowspan=2, sticky="e")

        body = ttk.Frame(self, padding=(18, 4, 18, 18))
        body.grid(row=1, column=0, sticky="nsew")
        body.columnconfigure(0, weight=1)
        body.rowconfigure(5, weight=1)

        setup = ttk.LabelFrame(body, text="Setup", padding=12)
        setup.grid(row=0, column=0, sticky="ew")
        setup.columnconfigure(1, weight=1)

        self._folder_row(setup, 0, "Input folder", self.input_dir, self._browse_input)
        self._folder_row(setup, 1, "Output folder", self.output_dir, self._browse_output)

        ttk.Label(setup, text="Filename").grid(row=2, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(setup, textvariable=self.output_filename).grid(row=2, column=1, sticky="ew", padx=(8, 8), pady=(8, 0))
        ttk.Button(setup, text="Sample", style="Tool.TButton", command=self._use_sample).grid(
            row=2, column=2, sticky="ew", pady=(8, 0)
        )

        options = ttk.Frame(body)
        options.grid(row=1, column=0, sticky="ew", pady=(12, 0))
        options.columnconfigure(3, weight=1)

        ttk.Label(options, text="Framerate").grid(row=0, column=0, sticky="w")
        ttk.Spinbox(options, from_=1, to=240, increment=1, textvariable=self.framerate, width=8).grid(
            row=0, column=1, sticky="w", padx=(8, 18)
        )

        ttk.Label(options, text="Node name").grid(row=0, column=2, sticky="w")
        ttk.Entry(options, textvariable=self.node_name, width=18).grid(row=0, column=3, sticky="w", padx=(8, 0))

        info = ttk.Frame(body)
        info.grid(row=2, column=0, sticky="ew", pady=(12, 0))
        info.columnconfigure(0, weight=1)

        ttk.Label(info, textvariable=self.sequence_text, style="Subtitle.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(info, textvariable=self.output_preview, style="Subtitle.TLabel").grid(row=1, column=0, sticky="w", pady=(4, 0))

        actions = ttk.Frame(body)
        actions.grid(row=3, column=0, sticky="ew", pady=(14, 0))
        actions.columnconfigure(0, weight=1)

        self.convert_button = ttk.Button(actions, text="Convert", style="Primary.TButton", command=self._start_conversion)
        self.convert_button.grid(row=0, column=0, sticky="ew")
        self.build_button = ttk.Button(actions, text="Build Converter", style="Tool.TButton", command=self._start_build)
        self.build_button.grid(row=0, column=1, padx=(8, 0))
        self.cancel_button = ttk.Button(actions, text="Cancel", style="Tool.TButton", command=self._cancel_job, state="disabled")
        self.cancel_button.grid(row=0, column=2, padx=(8, 0))
        self.reveal_button = ttk.Button(actions, text="Reveal Output", style="Tool.TButton", command=self._reveal_output, state="disabled")
        self.reveal_button.grid(row=0, column=3, padx=(8, 0))

        progress_row = ttk.Frame(body)
        progress_row.grid(row=4, column=0, sticky="ew", pady=(10, 0))
        progress_row.columnconfigure(0, weight=1)

        self.progress = ttk.Progressbar(progress_row, mode="indeterminate")
        self.progress.grid(row=0, column=0, sticky="ew")

        self.progress_label = tk.StringVar(value="")
        ttk.Label(progress_row, textvariable=self.progress_label, style="Subtitle.TLabel").grid(
            row=0, column=1, padx=(10, 0)
        )

        log_frame = ttk.LabelFrame(body, text="Log", padding=8)
        log_frame.grid(row=5, column=0, sticky="nsew", pady=(12, 0))
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)

        self.log = tk.Text(log_frame, height=12, wrap="word", state="disabled", borderwidth=0)
        self.log.grid(row=0, column=0, sticky="nsew")

        scroll = ttk.Scrollbar(log_frame, orient="vertical", command=self.log.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        self.log.configure(yscrollcommand=scroll.set)

    def _folder_row(self, parent, row, label, variable, command):
        pady = (0, 0) if row == 0 else (8, 0)
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=pady)
        ttk.Entry(parent, textvariable=variable).grid(row=row, column=1, sticky="ew", padx=(8, 8), pady=pady)
        ttk.Button(parent, text="Browse", style="Tool.TButton", command=command).grid(row=row, column=2, sticky="ew", pady=pady)

    def _browse_input(self):
        directory = filedialog.askdirectory(title="Choose the folder containing OBJ files")
        if directory:
            self.input_dir.set(directory)
            self._inspect_sequence()

    def _browse_output(self):
        directory = filedialog.askdirectory(title="Choose the output folder")
        if directory:
            self.output_dir.set(directory)

    def _use_sample(self):
        if SAMPLE_DIR.is_dir():
            self.input_dir.set(str(SAMPLE_DIR))
            self.output_dir.set(str(DEFAULT_OUTPUT_DIR))
            self.output_filename.set("head-poses.abc")
            self._inspect_sequence()
        else:
            messagebox.showerror("Sample missing", "The head-poses sample folder is not included in this copy.")

    def _append_log(self, text):
        self.log.configure(state="normal")
        self.log.insert("end", text)
        self.log.see("end")
        self.log.configure(state="disabled")

    def _clear_log(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _refresh_converter_status(self):
        if converter_ready():
            self.status_text.set("Converter bundled" if IS_FROZEN else "Converter ready")
            self.status_badge.configure(bg="#dcfce7", fg="#166534")
            self.build_button.configure(text="Bundled" if IS_FROZEN else "Rebuild")
        else:
            self.status_text.set("Converter missing" if IS_FROZEN else "Build needed")
            self.status_badge.configure(bg="#fef3c7", fg="#92400e")
            self.build_button.configure(text="Build Converter")
        if IS_FROZEN:
            self.build_button.configure(state="disabled")

    def _inspect_sequence(self):
        folder = Path(self.input_dir.get()).expanduser()
        files = obj_files(folder)
        if not folder.is_dir():
            self.sequence_text.set("Choose an input folder to inspect the sequence.")
            return
        if not files:
            self.sequence_text.set("No OBJ files found in this folder.")
            return

        first_counts = inspect_obj(files[0])
        uv_text = "UVs found" if first_counts["vt"] else "no UVs"
        normal_text = "normals found" if first_counts["vn"] else "no normals"
        self.sequence_text.set(
            f"{len(files)} OBJ frame(s): {files[0].name} -> {files[-1].name} - "
            f"{first_counts['v']} vertices, {first_counts['f']} faces, {uv_text}, {normal_text}"
        )

    def _refresh_preview(self):
        folder_text = self.input_dir.get().strip()
        output_dir_text = self.output_dir.get().strip()
        filename = self.output_filename.get().strip() or "output.abc"
        if not filename.lower().endswith(".abc"):
            filename += ".abc"

        if output_dir_text:
            output_dir = Path(output_dir_text).expanduser()
        elif folder_text:
            output_dir = Path(folder_text).expanduser().parent / "output"
        else:
            self.output_preview.set("Output path will appear here.")
            return

        self.output_preview.set(f"Output: {output_dir / filename}")
        if folder_text:
            self._inspect_sequence()

    def _validated_conversion(self):
        input_path = Path(self.input_dir.get()).expanduser()
        if not input_path.is_dir():
            messagebox.showerror("Input folder missing", "Choose an existing input folder containing OBJ files.")
            return None

        files = obj_files(input_path)
        if not files:
            messagebox.showerror("No OBJ files found", "The input folder does not contain any .obj files.")
            return None

        filename = self.output_filename.get().strip() or "output.abc"
        if not filename.lower().endswith(".abc"):
            filename += ".abc"

        output_dir_text = self.output_dir.get().strip()
        output_dir = Path(output_dir_text).expanduser() if output_dir_text else input_path.parent / "output"

        try:
            fps = float(self.framerate.get())
        except ValueError:
            messagebox.showerror("Invalid framerate", "Framerate must be a number.")
            return None

        if fps <= 0:
            messagebox.showerror("Invalid framerate", "Framerate must be greater than zero.")
            return None

        node_name = self.node_name.get().strip() or "Mesh"

        if not converter_ready():
            messagebox.showerror(
                "Converter not built",
                "The converter binary is missing. Click Build Converter, then try again.",
            )
            return None

        return input_path, output_dir, output_dir / filename, fps, node_name

    def _start_conversion(self):
        values = self._validated_conversion()
        if values is None:
            return

        input_path, output_dir, output_path, fps, node_name = values
        output_dir.mkdir(parents=True, exist_ok=True)
        self.last_output_path = output_path

        command = [
            str(BINARY_PATH),
            "-i",
            str(input_path),
            "-o",
            str(output_path),
            "-f",
            str(fps),
            "-n",
            node_name,
        ]

        self._start_job(command, "convert")

    def _start_build(self):
        if IS_FROZEN:
            messagebox.showinfo("Converter bundled", "This Windows app already includes the converter.")
            return
        if not BUILD_SCRIPT.exists():
            messagebox.showerror("Build script missing", "build.ps1 is missing from this copy of the tool.")
            return
        command = [
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(BUILD_SCRIPT),
        ]
        self._start_job(command, "build")

    def _start_job(self, command, kind):
        self._clear_log()
        self._append_log("> " + format_command(command) + "\n\n")
        self.current_job = kind
        self.total_frames = 0
        self.progress_label.set("")
        self._set_running(True)
        thread = threading.Thread(target=self._run_process, args=(command,), daemon=True)
        thread.start()

    def _set_running(self, running):
        state = "disabled" if running else "normal"
        self.convert_button.configure(state=state)
        self.build_button.configure(state="disabled" if running or IS_FROZEN else "normal")
        self.cancel_button.configure(state="normal" if running else "disabled")
        if running:
            if self.current_job == "convert":
                self.progress.configure(mode="determinate")
                self.progress["value"] = 0
            else:
                self.progress.configure(mode="indeterminate")
                self.progress.start(12)
        else:
            self.progress.stop()
            self.progress.configure(mode="determinate")
            self.progress["value"] = 0
            self.progress_label.set("")

    def _run_process(self, command):
        try:
            creationflags = 0
            if os.name == "nt":
                creationflags = subprocess.CREATE_NEW_PROCESS_GROUP

            self.process = subprocess.Popen(
                command,
                cwd=str(APP_DIR),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
                creationflags=creationflags,
            )

            assert self.process.stdout is not None
            for line in self.process.stdout:
                self.log_queue.put(("log", line))

            return_code = self.process.wait()
            self.log_queue.put(("done", self.current_job, return_code))
        except Exception as exc:
            self.log_queue.put(("error", str(exc)))
        finally:
            self.process = None

    def _cancel_job(self):
        if self.process and self.process.poll() is None:
            self._append_log("\nCancelling...\n")
            self.process.terminate()

    def _reveal_output(self):
        if self.last_output_path and self.last_output_path.exists():
            subprocess.run(["explorer.exe", f'/select,"{self.last_output_path}"'], check=False)
            return
        output_dir_text = self.output_dir.get().strip()
        if output_dir_text and Path(output_dir_text).expanduser().exists():
            subprocess.run(["explorer.exe", str(Path(output_dir_text).expanduser())], check=False)

    def _drain_log_queue(self):
        try:
            while True:
                message = self.log_queue.get_nowait()
                kind = message[0]
                if kind == "log":
                    line = message[1]
                    if self.current_job == "convert" and line.startswith("PROGRESS "):
                        parts = line.split()
                        if len(parts) == 3:
                            try:
                                current = int(parts[1])
                                total = int(parts[2])
                                if total > 0:
                                    self.total_frames = total
                                    self.progress["value"] = (current / total) * 100
                                    self.progress_label.set(f"{current} / {total}")
                            except ValueError:
                                pass
                    else:
                        self._append_log(line)
                elif kind == "done":
                    _, job, return_code = message
                    self._set_running(False)
                    self._refresh_converter_status()
                    if return_code == 0 and job == "convert":
                        self.reveal_button.configure(state="normal")
                        messagebox.showinfo("Conversion complete", "Alembic file created successfully.")
                    elif return_code == 0 and job == "build":
                        messagebox.showinfo("Build complete", "The converter is ready.")
                    else:
                        label = "Build" if job == "build" else "Conversion"
                        messagebox.showerror(f"{label} failed", f"{label} exited with code {return_code}.")
                    self.current_job = None
                elif kind == "error":
                    self._set_running(False)
                    self._refresh_converter_status()
                    messagebox.showerror("Operation failed", message[1])
                    self.current_job = None
        except queue.Empty:
            pass

        self.after(100, self._drain_log_queue)


if __name__ == "__main__":
    app = ObjToAlembicApp()
    app.mainloop()
