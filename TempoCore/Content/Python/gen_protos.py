"""
Generate protobuf code for Tempo plugin modules.
Copyright Tempo Simulation, LLC. All Rights Reserved
"""

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, Set, Optional, List

from prebuild_cache import PrebuildCache, find_files_filtered


class ProtoGenerator:
    def __init__(self, args):
        self.engine_dir = Path(args.engine_dir)
        self.project_file = Path(args.project_file)
        self.project_root = Path(args.project_root)
        self.plugin_root = Path(args.plugin_root)
        self.target_name = args.target_name
        self.target_config = args.target_config
        self.target_platform = args.target_platform
        self.tool_dir = Path(args.tool_dir)
        self.ubt_dir = Path(args.ubt_dir)
        
        self.python_dest_dir = self.plugin_root / "Content/Python/API/tempo"
        self.rust_proto_dir = self.plugin_root / "Content/Rust/API/proto"
        self.cpp_proto_dir = self.plugin_root / "Content/Cpp/API/proto"
        self.protoc = self.tool_dir / ("protoc.exe" if platform.system() == "Windows" else "protoc")
        self.grpc_cpp_plugin = self.tool_dir / ("grpc_cpp_plugin.exe" if platform.system() == "Windows" else "grpc_cpp_plugin")
        self.grpc_python_plugin = self.tool_dir / ("grpc_python_plugin.exe" if platform.system() == "Windows" else "grpc_python_plugin")

        self.temp_dir = None
        # Two parallel proto staging trees:
        #   src_temp_real_dir   — protos placed under their real (on-disk) module
        #                         name; used for the internal C++ pipeline that
        #                         deposits .pb.h into the plugin source tree.
        #   src_temp_virtual_dir — protos placed under their virtual module name
        #                         (after override) with imports rewritten to
        #                         match. Drives Python output and Cpp/Rust API
        #                         exports — the user-facing artifacts.
        # The proto's `package` declaration is identical in both, so the wire
        # identity (gRPC method paths, Any URLs) is unaffected by overrides.
        self.src_temp_real_dir = None
        self.src_temp_virtual_dir = None
        self.includes_real_dir = None
        self.includes_virtual_dir = None
        self.gen_temp_dir = None
        self.module_info = {}

        self.overrides_path = self.project_root / "Config" / "tempo_package_name_overrides.json"
        self.overrides = self._load_overrides()
        # virtual module name -> real (on-disk) module name that owns its source protos
        self.virtual_to_real: Dict[str, str] = {}

        self.cache = PrebuildCache(self.plugin_root / ".tempo_prebuild_cache.json")

    def collect_input_files(self) -> List[Path]:
        """Collect all files that affect protobuf generation."""
        files = [Path(__file__).resolve()]  # gen_protos.py itself
        files.extend(find_files_filtered(self.project_root, {'.proto', '.Build.cs', 'ttp_manifest.json'}))
        if self.overrides_path.exists():
            files.append(self.overrides_path)
        return files

    def _load_overrides(self) -> Dict[str, str]:
        """Load per-proto path overrides from the project's Config directory.

        The file is keyed by proto basename (e.g. "Camera.proto") or by a
        Subdir-qualified form (e.g. "Public/Camera.proto") for the rare
        basename-collision case. Values are the *virtual module name* the
        proto should appear as in user-facing artifacts (Python output dir,
        Cpp/Rust API export trees, generated #include paths).

        Overrides DO NOT change the proto's `package` declaration. The package
        is the wire identity (gRPC method routing, Any URLs) and must agree
        between server and client; it is sourced from the proto file itself.
        """
        if not self.overrides_path.exists():
            return {}
        try:
            with open(self.overrides_path, 'r', encoding='utf-8') as f:
                data = json.load(f)
        except (json.JSONDecodeError, OSError) as e:
            print(f"Warning: Could not parse {self.overrides_path}: {e}", file=sys.stderr)
            return {}
        if not isinstance(data, dict):
            print(f"Warning: {self.overrides_path} must be a JSON object, ignoring", file=sys.stderr)
            return {}
        return {str(k): str(v) for k, v in data.items()}

    def _virtual_module_for(self, real_module: str, relative_path: Path) -> str:
        """Resolve a proto file's virtual module name (post-override).

        Tries 'Subdir/Filename.proto' first (more specific), then
        'Filename.proto', then falls back to the real module name.
        """
        full_key = str(relative_path).replace("\\", "/")
        if full_key in self.overrides:
            return self.overrides[full_key]
        if relative_path.name in self.overrides:
            return self.overrides[relative_path.name]
        return real_module

    def validate_overrides(self):
        """Warn about override keys that don't match any current proto file."""
        if not self.overrides:
            return
        all_keys = set()
        for module_data in self.module_info.values():
            module_path = Path(module_data["Directory"].strip('"').replace("\\", "/"))
            for sub in ("Public", "Private"):
                sub_dir = module_path / sub
                if not sub_dir.exists():
                    continue
                for proto_file in sub_dir.rglob("*.proto"):
                    all_keys.add(proto_file.name)
                    all_keys.add(str(proto_file.relative_to(sub_dir)).replace("\\", "/"))
        for key in self.overrides:
            if key not in all_keys:
                print(
                    f"Warning: tempo_package_name_overrides.json key '{key}' "
                    f"matches no proto file",
                    file=sys.stderr,
                )

    def collect_output_files(self) -> List[Path]:
        """Collect all generated protobuf files.

        When Cpp/Rust API generation is opted in we also include the exported
        proto trees, so the cache invalidates if the export goes stale (e.g.,
        the user enables an opt-in for the first time, or a proto rename
        leaves orphan files in the export dir from a prior opt-in run).
        """
        files = find_files_filtered(self.project_root, {'.pb.h', '.pb.cc', '_pb2.py', '_pb2_grpc.py'})
        if self._cpp_opt_in() and self.cpp_proto_dir.exists():
            files.extend(self.cpp_proto_dir.rglob("*.proto"))
        if self._rust_opt_in() and self.rust_proto_dir.exists():
            files.extend(self.rust_proto_dir.rglob("*.proto"))
        return files

    def setup_python_dest(self):
        """Create Python destination directory and __init__.py"""
        self.python_dest_dir.mkdir(parents=True, exist_ok=True)
        (self.python_dest_dir / "__init__.py").touch()

    def setup_temp_dirs(self):
        """Create temporary directory structure for the two-pipeline build."""
        self.temp_dir = Path(tempfile.mkdtemp())
        self.src_temp_real_dir = self.temp_dir / "SourceReal"
        self.src_temp_virtual_dir = self.temp_dir / "SourceVirtual"
        self.includes_real_dir = self.temp_dir / "IncludesReal"
        self.includes_virtual_dir = self.temp_dir / "IncludesVirtual"
        self.gen_temp_dir = self.temp_dir / "Generated"

        for d in (self.src_temp_real_dir, self.src_temp_virtual_dir,
                  self.includes_real_dir, self.includes_virtual_dir,
                  self.gen_temp_dir):
            d.mkdir()

    def cleanup_temp_dirs(self):
        """Remove temporary directories"""
        if self.temp_dir and self.temp_dir.exists():
            shutil.rmtree(self.temp_dir)

    def setup_tool_permissions(self):
        """Set executable permissions on Linux"""
        if platform.system() == "Linux":
            for tool in [self.protoc, self.grpc_cpp_plugin, self.grpc_python_plugin]:
                if tool.exists():
                    tool.chmod(tool.stat().st_mode | 0o111)

    def replace_if_stale(self, fresh: Path, possibly_stale: Path):
        """Copy file only if it doesn't exist or differs from source"""
        if not possibly_stale.exists():
            possibly_stale.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(fresh, possibly_stale)
        else:
            if not self._files_identical(fresh, possibly_stale):
                shutil.copy2(fresh, possibly_stale)

    @staticmethod
    def _files_identical(file1: Path, file2: Path) -> bool:
        """Compare two files for equality"""
        if file1.stat().st_size != file2.stat().st_size:
            return False
        
        chunk_size = 8192
        with open(file1, 'rb') as f1, open(file2, 'rb') as f2:
            while True:
                chunk1 = f1.read(chunk_size)
                chunk2 = f2.read(chunk_size)
                if chunk1 != chunk2:
                    return False
                if not chunk1:
                    return True

    def find_dotnet(self) -> Path:
        """Locate the dotnet executable in the engine directory"""
        system = platform.system()
        
        if system == "Windows":
            pattern = "**/dotnet.exe"
        else:
            pattern = "**/dotnet"
        
        dotnet_dir = self.engine_dir / "Binaries/ThirdParty/DotNet"
        if not dotnet_dir.exists():
            raise RuntimeError(f"DotNet directory not found: {dotnet_dir}")
        
        dotnets = list(dotnet_dir.glob(pattern))
        if not dotnets:
            raise RuntimeError(f"dotnet executable not found in {dotnet_dir}")
        
        if system == "Windows":
            return dotnets[0]
        
        # On Mac/Linux, find the appropriate architecture
        machine = platform.machine().lower()
        
        if system == "Darwin":
            if machine == "arm64":
                pattern = "mac-arm64"
            else:
                pattern = "mac-x64"
        elif system == "Linux":
            # Check engine version for 5.4 compatibility
            release = self._get_engine_release()
            if release == "5.4":
                return dotnets[0]
            
            if machine in ["arm64", "aarch64"]:
                pattern = "linux-arm64"
            else:
                pattern = "linux-x64"
        
        for dotnet in dotnets:
            if pattern in str(dotnet):
                return dotnet
        
        raise RuntimeError(f"Could not find dotnet for platform {system}/{machine}")

    def _get_engine_release(self) -> Optional[str]:
        """Get the engine release version (e.g., '5.4')"""
        manifest_path = self.engine_dir / "Intermediate/Build/BuildRules/UE5RulesManifest.json"
        if manifest_path.exists():
            try:
                with open(manifest_path) as f:
                    data = json.load(f)
                    version = data.get("EngineVersion", "")
                    # Extract major.minor from version like "5.4.1"
                    parts = version.split(".")
                    if len(parts) >= 2:
                        return f"{parts[0]}.{parts[1]}"
            except (json.JSONDecodeError, KeyError) as e:
                print(f"Warning: Could not parse engine version: {e}", file=sys.stderr)
        return None

    def export_module_dependencies(self):
        """Run UnrealBuildTool to export module dependency information"""
        dotnet = self.find_dotnet()
        ubt_dll = self.ubt_dir / "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"
        
        if not ubt_dll.exists():
            raise RuntimeError(f"UnrealBuildTool not found: {ubt_dll}")
        
        output_file = self.temp_dir / "TempoModules.json"
        
        cmd = [
            str(dotnet),
            str(ubt_dll),
            "-Mode=JsonExport",
            self.target_name,
            self.target_platform,
            self.target_config,
            f"-Project={self.project_file}",
            f"-OutputFile={output_file}",
            "-NoMutex",
            f"-rootdirectory={self.engine_dir.parent}"
        ]
        
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                check=True
            )
        except subprocess.CalledProcessError as e:
            print(f"Error: UnrealBuildTool failed with exit code {e.returncode}", file=sys.stderr)
            if e.stdout:
                print(f"stdout: {e.stdout}", file=sys.stderr)
            if e.stderr:
                print(f"stderr: {e.stderr}", file=sys.stderr)
            raise RuntimeError("Failed to export module dependencies")
        
        if not output_file.exists():
            raise RuntimeError(f"UnrealBuildTool did not create output file: {output_file}")
        
        return output_file

    def filter_project_modules(self, json_file: Path) -> Dict:
        """Extract C++ project modules from the JSON export"""
        with open(json_file) as f:
            data = json.load(f)
        
        if "Modules" not in data:
            raise RuntimeError("Invalid module JSON: 'Modules' key not found")
        
        modules = data["Modules"]
        filtered = {}
        
        # Normalize paths for comparison
        project_root_normalized = str(self.project_root).replace("\\", "/")
        if not project_root_normalized.startswith("/"):
            project_root_normalized = "/" + project_root_normalized
        
        for module_name, module_data in modules.items():
            if module_data.get("Type") != "CPlusPlus":
                continue
            
            module_dir = module_data.get("Directory", "")
            module_dir_normalized = module_dir.replace("\\", "/")
            if not module_dir_normalized.startswith("/"):
                module_dir_normalized = "/" + module_dir_normalized
            
            if module_dir_normalized.startswith(project_root_normalized):
                filtered[module_name] = {
                    "Directory": module_dir,
                    "PublicDependencyModules": module_data.get("PublicDependencyModules", []),
                    "PrivateDependencyModules": module_data.get("PrivateDependencyModules", [])
                }
        
        return filtered

    def copy_module_protos(self, real_module_name: str, module_path: Path):
        """Stage a real module's protos into both the real and virtual trees.

        - Real tree (src_temp_real_dir): proto placed under the real module
          name; package fallback is the real module name; imports left as
          authored. Drives internal C++ generation.
        - Virtual tree (src_temp_virtual_dir): proto placed under its virtual
          (override-resolved) module name; same package fallback; imports
          rewritten to match override-controlled paths. Drives Python output
          and Cpp/Rust API exports.

        The package declaration itself is never modified by overrides. A
        proto's source-declared `package X;` is the wire identity (gRPC
        method routing, Any URLs) and must be identical in both pipelines so
        client and server agree. The override file controls file paths only.

        Multiple virtuals may share one real module (the collapse case), but
        a single virtual must not span real modules.
        """
        for sub in ("Public", "Private"):
            sub_dir = module_path / sub
            if not sub_dir.exists():
                continue
            for proto_file in sub_dir.rglob("*.proto"):
                relative_path = proto_file.relative_to(sub_dir)

                # --- Internal pipeline: real-keyed, no transforms.
                real_dest = self.src_temp_real_dir / real_module_name / sub / relative_path
                real_dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(proto_file, real_dest)
                self._ensure_package_declaration(real_dest, fallback=real_module_name)

                # --- External pipeline: virtual-keyed, imports rewritten.
                virtual_module = self._virtual_module_for(real_module_name, relative_path)
                existing_real = self.virtual_to_real.get(virtual_module)
                if existing_real is not None and existing_real != real_module_name:
                    raise RuntimeError(
                        f"Override conflict: virtual module '{virtual_module}' is "
                        f"claimed by both real module '{existing_real}' and "
                        f"'{real_module_name}'. Each override target must be sourced "
                        f"from a single real module."
                    )
                self.virtual_to_real[virtual_module] = real_module_name

                virtual_dest = self.src_temp_virtual_dir / virtual_module / sub / relative_path
                virtual_dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(proto_file, virtual_dest)
                self._ensure_package_declaration(virtual_dest, fallback=real_module_name)
                self._rewrite_imports_for_overrides(virtual_dest)

    def _rewrite_imports_for_overrides(self, proto_file: Path):
        """Retarget `import` statements to follow override-driven virtual paths.

        An import like `import "X/Y.proto";` becomes `import "<override>/Y.proto";`
        when Y.proto is overridden. Imports for non-overridden basenames are
        left untouched (including google/protobuf well-knowns, which are not
        in the override map). This keeps generated descriptors and exported
        proto trees consistent with the override file without forcing users
        to edit Tempo source protos.
        """
        if not self.overrides:
            return
        with open(proto_file, 'r', encoding='utf-8') as f:
            content = f.read()

        import_pattern = re.compile(
            r'^(\s*import\s+")(?:[^"]*/)?([^"/]+\.proto)(";\s*)$',
            re.MULTILINE,
        )

        def replace(match):
            prefix, basename, suffix = match.group(1), match.group(2), match.group(3)
            if basename in self.overrides:
                return f'{prefix}{self.overrides[basename]}/{basename}{suffix}'
            return match.group(0)

        new_content, n = import_pattern.subn(replace, content)
        if n:
            with open(proto_file, 'w', encoding='utf-8') as f:
                f.write(new_content)

    def _ensure_package_declaration(self, proto_file: Path, fallback: str):
        """Add a `package` declaration if the proto doesn't already have one.

        Source-declared packages are preserved verbatim (no nesting, no
        rewriting). The package is the wire identity and must be stable
        across the v2 refactor — protos that need to keep a v1 package after
        being moved between modules should declare it explicitly. Only
        protos with no declaration at all fall back to the real module name.
        """
        with open(proto_file, 'r', encoding='utf-8') as f:
            content = f.read()

        if re.search(r'^\s*package\s+', content, re.MULTILINE):
            return

        with open(proto_file, 'w', encoding='utf-8') as f:
            f.write(content + f"\npackage {fallback};\n")

    def sync_protos(self, src: Path, dest: Path):
        """Copy .proto files from src to dest, preserving directory structure"""
        if not src.exists():
            return
        
        for proto_file in src.rglob("*.proto"):
            relative_path = proto_file.relative_to(src)
            dest_file = dest / relative_path
            dest_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(proto_file, dest_file)

    def _collect_real_module_deps(self, real_module: str, visited: Set[str]):
        """Walk a real module's public dep tree, collecting transitively-reachable
        project modules (those present in module_info)."""
        if real_module in visited:
            return
        if real_module not in self.module_info:
            return
        visited.add(real_module)
        for dep in self.module_info[real_module].get("PublicDependencyModules", []):
            self._collect_real_module_deps(dep.strip().rstrip('\r'), visited)

    @staticmethod
    def _proto_has_service(proto_file: Path) -> bool:
        with open(proto_file, 'r', encoding='utf-8') as f:
            return bool(re.search(r'\bservice\s+', f.read()))

    def _setup_real_includes(self, real_module: str) -> Path:
        """Build a protoc include tree for the internal C++ pipeline.

        Layout: <includes>/<real_module>/<proto files>. The module being
        compiled gets Public + Private; transitively reachable real-module
        deps get Public only. Mirrors the historical behavior — overrides
        play no role here.
        """
        includes_dir = self.includes_real_dir / real_module
        includes_dir.mkdir(parents=True, exist_ok=True)

        self.sync_protos(self.src_temp_real_dir / real_module / "Public", includes_dir / real_module)
        self.sync_protos(self.src_temp_real_dir / real_module / "Private", includes_dir / real_module)

        reachable: Set[str] = set()
        for dep in (self.module_info.get(real_module, {}).get("PublicDependencyModules", []) +
                    self.module_info.get(real_module, {}).get("PrivateDependencyModules", [])):
            self._collect_real_module_deps(dep.strip().rstrip('\r'), reachable)

        for dep_real in reachable:
            if dep_real == real_module:
                continue
            dep_dir = includes_dir / dep_real
            if not dep_dir.exists():
                dep_dir.mkdir(parents=True, exist_ok=True)
                self.sync_protos(self.src_temp_real_dir / dep_real / "Public", dep_dir)

        return includes_dir

    def _setup_virtual_includes(self, virtual_module: str) -> Path:
        """Build a protoc include tree for the external (Python+export) pipeline.

        Layout: <includes>/<virtual>/<proto files>. Self gets Public + Private;
        sibling virtuals on the same real module + transitively reachable
        virtuals (via their backing real modules) get Public only.
        """
        real_module = self.virtual_to_real[virtual_module]
        includes_dir = self.includes_virtual_dir / virtual_module
        includes_dir.mkdir(parents=True, exist_ok=True)

        self.sync_protos(self.src_temp_virtual_dir / virtual_module / "Public", includes_dir / virtual_module)
        self.sync_protos(self.src_temp_virtual_dir / virtual_module / "Private", includes_dir / virtual_module)

        # Sibling virtuals on the same real module — they were neighbors in the
        # source layout and may import each other.
        for vmod, rmod in self.virtual_to_real.items():
            if vmod == virtual_module or rmod != real_module:
                continue
            dep_dir = includes_dir / vmod
            if not dep_dir.exists():
                dep_dir.mkdir(parents=True, exist_ok=True)
                self.sync_protos(self.src_temp_virtual_dir / vmod / "Public", dep_dir)

        reachable_reals: Set[str] = set()
        for dep in (self.module_info.get(real_module, {}).get("PublicDependencyModules", []) +
                    self.module_info.get(real_module, {}).get("PrivateDependencyModules", [])):
            self._collect_real_module_deps(dep.strip().rstrip('\r'), reachable_reals)

        for vmod, rmod in self.virtual_to_real.items():
            if vmod == virtual_module or rmod not in reachable_reals:
                continue
            dep_dir = includes_dir / vmod
            if not dep_dir.exists():
                dep_dir.mkdir(parents=True, exist_ok=True)
                self.sync_protos(self.src_temp_virtual_dir / vmod / "Public", dep_dir)

        return includes_dir

    def generate_cpp_for_real_module(self, real_module_name: str):
        """Run the internal C++ generation pipeline for one real module.

        Generated .pb.h files land at <real>/Public/ProtobufGenerated/<real>/
        so internal Tempo source uses real-module #include paths regardless
        of any user override file.
        """
        real_module_data = self.module_info[real_module_name]
        real_module_path = Path(real_module_data["Directory"].strip('"').replace("\\", "/"))

        public_dest_dir = real_module_path / "Public/ProtobufGenerated"
        private_dest_dir = real_module_path / "Private/ProtobufGenerated"
        real_src_dir = self.src_temp_real_dir / real_module_name

        # We invoke protoc on copies under the include tree so the file path is
        # always reachable from the -I root (protoc errors otherwise). Public vs
        # Private deposit decisions still consult src_temp_real_dir, where the
        # original sub-directory split is preserved.
        has_protos = real_src_dir.exists() and any(real_src_dir.rglob("*.proto"))
        refreshed: Set[Path] = set()

        if has_protos:
            includes_dir = self._setup_real_includes(real_module_name)
            module_includes_dir = includes_dir / real_module_name
            proto_files = sorted(module_includes_dir.rglob("*.proto"))
            cpp_temp_dir = self.gen_temp_dir / "cpp" / real_module_name
            cpp_temp_dir.mkdir(parents=True, exist_ok=True)
            export_macro = f"{real_module_name.upper()}_API"
            for proto_file in proto_files:
                self._generate_proto_cpp(proto_file, includes_dir, cpp_temp_dir, export_macro)

            for generated_file in cpp_temp_dir.rglob("*"):
                if not generated_file.is_file():
                    continue
                relative_path = generated_file.relative_to(cpp_temp_dir)
                if generated_file.name.endswith(".pb.h"):
                    proto_basename = generated_file.name.split('.')[0] + ".proto"
                    if (real_src_dir / "Public" / proto_basename).exists():
                        dest_file = public_dest_dir / relative_path
                    else:
                        dest_file = private_dest_dir / relative_path
                else:
                    dest_file = private_dest_dir / relative_path
                self.replace_if_stale(generated_file, dest_file)
                refreshed.add(dest_file)

        # Sweep stale .pb.* files in the real module's tree and prune empty dirs.
        for cpp_file in list(real_module_path.rglob("*.pb.*")):
            if cpp_file not in refreshed:
                cpp_file.unlink()
        self._remove_empty_dirs(public_dest_dir)
        self._remove_empty_dirs(private_dest_dir)

    def generate_python_for_virtual_module(self, virtual_module: str) -> Set[Path]:
        """Run the external Python generation pipeline for one virtual module.

        Outputs land at python_dest_dir/<virtual>/<file>_pb2.py — the file
        path follows the override so user clients' import paths can be
        preserved across the v2 refactor. Returns the set of files refreshed
        so the caller can drive the global stale-file sweep.
        """
        virtual_src_dir = self.src_temp_virtual_dir / virtual_module
        if not virtual_src_dir.exists() or not any(virtual_src_dir.rglob("*.proto")):
            return set()

        includes_dir = self._setup_virtual_includes(virtual_module)
        # Iterate copies under the include tree (see C++ pipeline for why).
        module_includes_dir = includes_dir / virtual_module
        proto_files = sorted(module_includes_dir.rglob("*.proto"))
        python_temp_dir = self.gen_temp_dir / "python" / virtual_module
        python_temp_dir.mkdir(parents=True, exist_ok=True)

        python_module_dir = self.python_dest_dir / virtual_module
        python_module_dir.mkdir(parents=True, exist_ok=True)
        (python_module_dir / "__init__.py").touch()

        for proto_file in proto_files:
            self._generate_proto_python(proto_file, includes_dir, python_temp_dir)

        refreshed: Set[Path] = set()
        for generated_file in python_temp_dir.rglob("*"):
            if not generated_file.is_file():
                continue
            relative_path = generated_file.relative_to(python_temp_dir)
            dest_file = self.python_dest_dir / relative_path
            self.replace_if_stale(generated_file, dest_file)
            refreshed.add(dest_file)

        return refreshed

    def _generate_proto_cpp(self, proto_file: Path, include_dir: Path, cpp_out: Path, export_macro: str):
        cpp_cmd = [
            str(self.protoc),
            f"--cpp_out=dllexport_decl={export_macro}:{cpp_out}",
            f"-I{include_dir}",
            str(proto_file),
        ]
        if self._proto_has_service(proto_file):
            cpp_cmd.insert(2, f"--grpc_out={cpp_out}")
            cpp_cmd.insert(3, f"--plugin=protoc-gen-grpc={self.grpc_cpp_plugin}")
        try:
            subprocess.run(cpp_cmd, check=True, capture_output=True, text=True)
        except subprocess.CalledProcessError as e:
            print(f"Error generating C++ code for {proto_file}:", file=sys.stderr)
            print(f"Command: {' '.join(cpp_cmd)}", file=sys.stderr)
            if e.stderr:
                print(f"stderr: {e.stderr}", file=sys.stderr)
            raise

    def _generate_proto_python(self, proto_file: Path, include_dir: Path, python_out: Path):
        python_cmd = [
            str(self.protoc),
            f"--python_out={python_out}",
            f"-I{include_dir}",
            str(proto_file),
        ]
        if self._proto_has_service(proto_file):
            python_cmd.insert(2, f"--grpc_out={python_out}")
            python_cmd.insert(3, f"--plugin=protoc-gen-grpc={self.grpc_python_plugin}")
        try:
            subprocess.run(python_cmd, check=True, capture_output=True, text=True)
        except subprocess.CalledProcessError as e:
            print(f"Error generating Python code for {proto_file}:", file=sys.stderr)
            print(f"Command: {' '.join(python_cmd)}", file=sys.stderr)
            if e.stderr:
                print(f"stderr: {e.stderr}", file=sys.stderr)
            raise

    @staticmethod
    def _remove_empty_dirs(directory: Path):
        """Recursively remove empty directories"""
        if not directory.exists():
            return

        for dirpath, dirnames, filenames in os.walk(directory, topdown=False):
            path = Path(dirpath)
            if not filenames and not dirnames and path != directory:
                path.rmdir()

    @staticmethod
    def _rust_opt_in() -> bool:
        """Whether the user has opted into Rust API generation."""
        return os.environ.get("TEMPO_GEN_RUST_API", "0") not in ("0", "")

    @staticmethod
    def _cpp_opt_in() -> bool:
        """Whether the user has opted into C++ API generation."""
        return os.environ.get("TEMPO_GEN_CPP_API", "0") not in ("0", "")

    def _export_decorated_protos(self, dest_dir: Path):
        """Copy virtual-pipeline protos to dest_dir for external client consumption.

        The protos in src_temp_virtual_dir have their source-declared package
        intact and their imports rewritten per the override file. Module dirs
        nest Public/ and Private/; consumers want them flattened under
        <dest_dir>/<VirtualModule>/.
        """
        if dest_dir.exists():
            shutil.rmtree(dest_dir)
        dest_dir.mkdir(parents=True, exist_ok=True)

        for module_dir in self.src_temp_virtual_dir.iterdir():
            if not module_dir.is_dir():
                continue

            module_name = module_dir.name
            dest_module_dir = dest_dir / module_name

            for sub in ("Public", "Private"):
                sub_dir = module_dir / sub
                if not sub_dir.exists():
                    continue
                for proto_file in sub_dir.rglob("*.proto"):
                    relative_path = proto_file.relative_to(sub_dir)
                    dest_file = dest_module_dir / relative_path
                    dest_file.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(proto_file, dest_file)

    def export_rust_protos(self):
        """Export decorated proto files for Rust tonic-build."""
        if not self._rust_opt_in():
            return
        self._export_decorated_protos(self.rust_proto_dir)
        print(f"Exported Rust protos to {self.rust_proto_dir}")

    def export_cpp_protos(self):
        """Export decorated proto files for the C++ wrapper build."""
        if not self._cpp_opt_in():
            return
        self._export_decorated_protos(self.cpp_proto_dir)
        print(f"Exported C++ protos to {self.cpp_proto_dir}")

    def run(self):
        """Main execution flow"""
        try:
            # Check for TEMPO_SKIP_PREBUILD
            skip_prebuild = os.environ.get("TEMPO_SKIP_PREBUILD", "")
            if skip_prebuild and skip_prebuild != "0":
                print(f"[Tempo Prebuild]  Skipping Tempo protobuf generation (TEMPO_SKIP_PREBUILD is {skip_prebuild})")
                return 0

            # If Rust or C++ generation is opted in but the corresponding proto dir
            # hasn't been populated yet (first time the user enables it), bypass the
            # cache so the export runs.
            need_rust_export = self._rust_opt_in() and not self.rust_proto_dir.exists()
            need_cpp_export = self._cpp_opt_in() and not self.cpp_proto_dir.exists()

            # Check cache to see if we can skip generation
            input_files = self.collect_input_files()
            output_files = self.collect_output_files()
            if not (need_rust_export or need_cpp_export) and self.cache.is_valid(
                    "gen_protos", input_files, output_files,
                    input_base=self.project_root, output_base=self.project_root):
                print("[Tempo Prebuild]  Skipping Tempo protobuf generation (no changes detected)", flush=True)
                return 0

            print("[Tempo Prebuild] Generating protobuf code", flush=True)

            # Setup
            self.setup_python_dest()
            self.setup_temp_dirs()
            self.setup_tool_permissions()

            # Export module dependencies
            json_file = self.export_module_dependencies()
            self.module_info = self.filter_project_modules(json_file)

            if not self.module_info:
                print("Warning: No C++ project modules found with .proto files", file=sys.stderr)
                return

            self.validate_overrides()

            # Stage protos into both the real (internal) and virtual
            # (external) temp trees.
            for module_name, module_data in self.module_info.items():
                module_path = Path(module_data["Directory"].strip('"').replace("\\", "/"))
                self.copy_module_protos(module_name, module_path)

            # Internal C++ pipeline: deposits .pb.h into each plugin's source
            # tree using real-module paths. Override-independent.
            for real_module_name in self.module_info:
                self.generate_cpp_for_real_module(real_module_name)

            # External pipeline: Python output (consumed by user clients) at
            # virtual paths.
            refreshed_py_files: Set[Path] = set()
            for virtual_module in sorted(self.virtual_to_real):
                refreshed_py_files |= self.generate_python_for_virtual_module(virtual_module)

            # Global stale-file sweep. Older runs (or runs with a different
            # override file) may have created subdirs that no longer get
            # written to — e.g., on a v1 -> v2 Tempo upgrade, dirs like
            # TempoCamera/, TempoMapQuery/, TempoScripting/ become orphaned
            # because their protos now live under collapsed module names.
            # Drop any *_pb2*.py we didn't refresh this run, then prune any
            # subdir whose only remaining content is __init__.py / __pycache__
            # (it has no useful files left). Custom user-added files in those
            # subdirs are preserved.
            removed_py = []
            for py_file in self.python_dest_dir.rglob("*_pb2*"):
                if py_file.is_file() and py_file not in refreshed_py_files:
                    removed_py.append(py_file)
                    py_file.unlink()
            removed_dirs = []
            for subdir in list(self.python_dest_dir.iterdir()):
                if not subdir.is_dir():
                    continue
                useful = [
                    f for f in subdir.rglob("*")
                    if f.is_file()
                    and f.name != "__init__.py"
                    and not str(f).endswith(".pyc")
                ]
                if not useful:
                    removed_dirs.append(subdir)
                    shutil.rmtree(subdir)
            if removed_py or removed_dirs:
                rel = lambda p: p.relative_to(self.python_dest_dir)
                if removed_dirs:
                    names = sorted(rel(d).as_posix() for d in removed_dirs)
                    print(f"[Tempo Prebuild] Removed {len(removed_dirs)} stale Python "
                          f"output {'directory' if len(removed_dirs) == 1 else 'directories'} "
                          f"under {self.python_dest_dir}: {', '.join(names)}",
                          flush=True)
                # Files removed but their containing dir survived (i.e. user has
                # custom non-pb2 content there). Worth a heads-up too.
                survivor_files = [f for f in removed_py if f.parent.exists()]
                if survivor_files:
                    print(f"[Tempo Prebuild] Removed {len(survivor_files)} stale "
                          f"_pb2 file(s) from surviving directories: "
                          f"{', '.join(sorted(rel(f).as_posix() for f in survivor_files))}",
                          flush=True)
            self._remove_empty_dirs(self.python_dest_dir)

            # Update cache after successful generation
            output_files = self.collect_output_files()
            self.cache.update("gen_protos", input_files, output_files,
                              input_base=self.project_root, output_base=self.project_root)

            # Export protos for Rust crate
            self.export_rust_protos()

            # Export protos for C++ wrapper build
            self.export_cpp_protos()

            print("Done")
            
        finally:
            self.cleanup_temp_dirs()


def main():
    parser = argparse.ArgumentParser(description="Generate protobuf code for Tempo plugin modules")
    parser.add_argument("engine_dir", help="Unreal Engine directory")
    parser.add_argument("project_file", help="Project file path")
    parser.add_argument("project_root", help="Project root directory")
    parser.add_argument("plugin_root", help="Plugin root directory")
    parser.add_argument("target_name", help="Build target name")
    parser.add_argument("target_config", help="Build configuration")
    parser.add_argument("target_platform", help="Target platform")
    parser.add_argument("tool_dir", help="Protoc tools directory")
    parser.add_argument("ubt_dir", help="UnrealBuildTool directory")
    
    args = parser.parse_args()
    
    try:
        generator = ProtoGenerator(args)
        generator.run()
        return 0
    except Exception as e:
        print(f"Fatal error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
