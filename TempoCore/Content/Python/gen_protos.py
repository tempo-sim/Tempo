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
from typing import Dict, Set, Optional


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
        self.protoc = self.tool_dir / ("protoc.exe" if platform.system() == "Windows" else "protoc")
        self.grpc_cpp_plugin = self.tool_dir / ("grpc_cpp_plugin.exe" if platform.system() == "Windows" else "grpc_cpp_plugin")
        self.grpc_python_plugin = self.tool_dir / ("grpc_python_plugin.exe" if platform.system() == "Windows" else "grpc_python_plugin")
        
        self.temp_dir = None
        self.src_temp_dir = None
        self.includes_temp_dir = None
        self.gen_temp_dir = None
        self.module_info = {}

    def setup_python_dest(self):
        """Create Python destination directory and __init__.py"""
        self.python_dest_dir.mkdir(parents=True, exist_ok=True)
        (self.python_dest_dir / "__init__.py").touch()

    def setup_temp_dirs(self):
        """Create temporary directory structure"""
        self.temp_dir = Path(tempfile.mkdtemp())
        self.src_temp_dir = self.temp_dir / "Source"
        self.includes_temp_dir = self.temp_dir / "Includes"
        self.gen_temp_dir = self.temp_dir / "Generated"
        
        self.src_temp_dir.mkdir()
        self.includes_temp_dir.mkdir()
        self.gen_temp_dir.mkdir()

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

    def copy_module_protos(self, module_name: str, module_path: Path):
        """Copy .proto files from module to temp directory and add package specifier"""
        module_src_temp_dir = self.src_temp_dir / module_name
        
        if module_src_temp_dir.exists():
            raise RuntimeError(f"Multiple modules named {module_name} found. Please rename one.")
        
        (module_src_temp_dir / "Public").mkdir(parents=True)
        (module_src_temp_dir / "Private").mkdir(parents=True)
        
        # Copy public protos
        public_dir = module_path / "Public"
        if public_dir.exists():
            for proto_file in public_dir.rglob("*.proto"):
                relative_path = proto_file.relative_to(module_path)
                dest_file = module_src_temp_dir / relative_path
                dest_file.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(proto_file, dest_file)
        
        # Copy private protos
        private_dir = module_path / "Private"
        if private_dir.exists():
            for proto_file in private_dir.rglob("*.proto"):
                relative_path = proto_file.relative_to(module_path)
                dest_file = module_src_temp_dir / relative_path
                dest_file.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(proto_file, dest_file)
        
        # Add/modify package specifier in all copied protos
        for proto_file in module_src_temp_dir.rglob("*.proto"):
            self._add_package_specifier(proto_file, module_name)

    def _add_package_specifier(self, proto_file: Path, module_name: str):
        """Add or modify package specifier in a .proto file"""
        with open(proto_file, 'r', encoding='utf-8') as f:
            content = f.read()
        
        package_pattern = re.compile(r'^\s*package\s+([^;\s]+)\s*;', re.MULTILINE)
        match = package_pattern.search(content)
        
        if match:
            # Replace existing package
            existing_package = match.group(1)
            new_package = f"package {module_name}.{existing_package};"
            content = package_pattern.sub(new_package, content)
        else:
            # Append new package declaration
            content += f"\npackage {module_name};\n"
        
        with open(proto_file, 'w', encoding='utf-8') as f:
            f.write(content)

    def sync_protos(self, src: Path, dest: Path):
        """Copy .proto files from src to dest, preserving directory structure"""
        if not src.exists():
            return
        
        for proto_file in src.rglob("*.proto"):
            relative_path = proto_file.relative_to(src)
            dest_file = dest / relative_path
            dest_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(proto_file, dest_file)

    def get_module_includes_public_only(self, module_name: str, includes_dir: Path, visited: Set[str]):
        """Recursively get public dependencies of a module"""
        if module_name in visited:
            return
        
        visited.add(module_name)
        
        public_deps = self.module_info.get(module_name, {}).get("PublicDependencyModules", [])
        
        for dep in public_deps:
            dep = dep.strip().rstrip('\r')
            
            # Only consider project modules
            if not (self.src_temp_dir / dep).exists():
                continue
            
            dep_dir = includes_dir / dep
            if not dep_dir.exists():
                # New dependency - add its public protos
                dep_dir.mkdir(parents=True, exist_ok=True)
                self.sync_protos(self.src_temp_dir / dep / "Public", dep_dir)
            
            # Recursively add its public dependencies
            self.get_module_includes_public_only(dep, includes_dir, visited)

    def get_module_includes(self, module_name: str, includes_dir: Path):
        """Get all includes needed for a module (public and private dependencies)"""
        # Copy this module's protos (both public and private)
        module_dir = includes_dir / module_name
        module_dir.mkdir(parents=True, exist_ok=True)
        self.sync_protos(self.src_temp_dir / module_name / "Public", module_dir)
        self.sync_protos(self.src_temp_dir / module_name / "Private", module_dir)
        
        visited = set()
        
        # Add public dependencies
        public_deps = self.module_info.get(module_name, {}).get("PublicDependencyModules", [])
        for dep in public_deps:
            dep = dep.strip().rstrip('\r')
            
            if not (self.src_temp_dir / dep).exists():
                continue
            
            dep_dir = includes_dir / dep
            if not dep_dir.exists():
                dep_dir.mkdir(parents=True, exist_ok=True)
                self.sync_protos(self.src_temp_dir / dep / "Public", dep_dir)
            
            self.get_module_includes_public_only(dep, includes_dir, visited)
        
        # Add private dependencies
        private_deps = self.module_info.get(module_name, {}).get("PrivateDependencyModules", [])
        for dep in private_deps:
            dep = dep.strip().rstrip('\r')
            
            if not (self.src_temp_dir / dep).exists():
                continue
            
            dep_dir = includes_dir / dep
            if not dep_dir.exists():
                dep_dir.mkdir(parents=True, exist_ok=True)
                self.sync_protos(self.src_temp_dir / dep / "Public", dep_dir)
            
            self.get_module_includes_public_only(dep, includes_dir, visited)

    def generate_module_protos(self, module_name: str, module_path: Path, include_dir: Path):
        """Generate C++ and Python protobuf code for a module"""
        private_dest_dir = module_path / "Private/ProtobufGenerated"
        public_dest_dir = module_path / "Public/ProtobufGenerated"
        source_dir = include_dir / module_name
        export_macro = f"{module_name.upper()}_API"
        
        # Create Python module directory
        python_module_dir = self.python_dest_dir / module_name
        python_module_dir.mkdir(parents=True, exist_ok=True)
        (python_module_dir / "__init__.py").touch()
        
        # Create temporary generation directories
        module_gen_temp_dir = self.gen_temp_dir / module_name
        cpp_temp_dir = module_gen_temp_dir / "CPP"
        python_temp_dir = module_gen_temp_dir / "Python"
        cpp_temp_dir.mkdir(parents=True, exist_ok=True)
        python_temp_dir.mkdir(parents=True, exist_ok=True)
        
        # Generate protobuf code
        proto_files = list(source_dir.rglob("*.proto"))
        if not proto_files:
            return  # No protos to generate
        
        for proto_file in proto_files:
            self._generate_proto(proto_file, include_dir, cpp_temp_dir, python_temp_dir, export_macro)
        
        # Refresh C++ files
        refreshed_cpp_files = set()
        for generated_file in cpp_temp_dir.rglob("*"):
            if not generated_file.is_file():
                continue
            
            relative_path = generated_file.relative_to(cpp_temp_dir)
            module_src_temp_dir = self.src_temp_dir / module_name
            
            if generated_file.name.endswith(".pb.h"):
                # Header files go to Public if from Public proto, otherwise Private
                proto_file = generated_file.name.rstrip(''.join(generated_file.suffixes)) + ".proto"                
                if (module_src_temp_dir / "Public" / proto_file).exists():
                    dest_file = public_dest_dir / relative_path
                else:
                    dest_file = private_dest_dir / relative_path
            else:
                dest_file = private_dest_dir / relative_path
            
            self.replace_if_stale(generated_file, dest_file)
            refreshed_cpp_files.add(dest_file)
        
        # Remove stale C++ files
        for cpp_file in list(module_path.rglob("*.pb.*")):
            if cpp_file not in refreshed_cpp_files:
                cpp_file.unlink()
        
        # Remove empty directories
        self._remove_empty_dirs(public_dest_dir)
        self._remove_empty_dirs(private_dest_dir)
        
        # Refresh Python files
        refreshed_py_files = set()
        for generated_file in python_temp_dir.rglob("*"):
            if not generated_file.is_file():
                continue
            
            relative_path = generated_file.relative_to(python_temp_dir)
            dest_file = self.python_dest_dir / relative_path
            self.replace_if_stale(generated_file, dest_file)
            refreshed_py_files.add(dest_file)
        
        # Remove stale Python files
        if python_module_dir.exists():
            for py_file in python_module_dir.rglob("*_pb2*.py"):
                if py_file not in refreshed_py_files:
                    py_file.unlink()
        
        # Remove top-level generated Python files
        for py_file in self.python_dest_dir.glob("*_pb2*"):
            if py_file.is_file():
                py_file.unlink()
        
        # Remove empty directories
        self._remove_empty_dirs(self.python_dest_dir)

    def _generate_proto(self, proto_file: Path, include_dir: Path, cpp_out: Path, python_out: Path, export_macro: str):
        """Generate C++ and Python code for a single .proto file"""
        # Check if proto defines a service
        has_service = False
        with open(proto_file, 'r', encoding='utf-8') as f:
            content = f.read()
            has_service = bool(re.search(r'\bservice\s+', content))
        
        # Generate C++ code
        cpp_cmd = [
            str(self.protoc),
            f"--cpp_out=dllexport_decl={export_macro}:{cpp_out}",
            f"-I{include_dir}",
            str(proto_file)
        ]
        
        if has_service:
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
        
        # Generate Python code
        python_cmd = [
            str(self.protoc),
            f"--python_out={python_out}",
            f"-I{include_dir}",
            str(proto_file)
        ]
        
        if has_service:
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

    def run(self):
        """Main execution flow"""
        try:
            print("Generating protobuf code...")
            
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
            
            # Copy all module protos to temp directory
            for module_name, module_data in self.module_info.items():
                module_path = Path(module_data["Directory"].strip('"').replace("\\", "/"))
                self.copy_module_protos(module_name, module_path)
            
            # Generate protos for each module
            for module_name, module_data in self.module_info.items():
                module_path = Path(module_data["Directory"].strip('"').replace("\\", "/"))
                module_includes_dir = self.includes_temp_dir / module_name
                
                self.get_module_includes(module_name, module_includes_dir)
                self.generate_module_protos(module_name, module_path, module_includes_dir)
            
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
