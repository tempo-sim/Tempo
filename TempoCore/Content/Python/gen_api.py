"""
Generate Python API for Tempo plugin.
Copyright Tempo Simulation, LLC. All Rights Reserved
"""

import argparse
import importlib
from pathlib import Path
import os
import subprocess
import sys
import venv
import re


class APIGenerator:
    def __init__(self, args):
        self.project_root = Path(args.project_root)
        self.plugin_root = Path(args.plugin_root)
        self.venv_dir = self.project_root / "TempoEnv"
        self.requirements_file = self.plugin_root / "Content/Python/requirements.txt"
        self.api_dir = self.plugin_root / "Content/Python/API"
        
    def get_venv_python(self) -> Path:
        """Get path to Python executable in virtual environment"""
        if sys.platform == "win32":
            return self.venv_dir / "Scripts" / "python.exe"
        else:
            return self.venv_dir / "bin" / "python3"
    
    def get_venv_pip(self) -> Path:
        """Get path to pip executable in virtual environment"""
        if sys.platform == "win32":
            return self.venv_dir / "Scripts" / "pip.exe"
        else:
            return self.venv_dir / "bin" / "pip"
    
    def needs_venv_recreation(self) -> bool:
        """Check if virtual environment needs to be recreated"""
        pyvenv_cfg = self.venv_dir / "pyvenv.cfg"
        
        if not pyvenv_cfg.exists():
            return True
        
        # Check if venv was created with current Python
        current_python_dir = str(Path(sys.executable).parent)
        
        with open(pyvenv_cfg, 'r') as f:
            for line in f:
                if line.startswith("home = "):
                    venv_python_dir = line.replace("home = ", "").strip().replace("\\", "/")
                    current_python_dir_normalized = current_python_dir.replace("\\", "/")
                    
                    if venv_python_dir != current_python_dir_normalized:
                        return True
                    break
        
        return False
    
    def create_venv(self):
        """Create or recreate virtual environment"""
        if self.venv_dir.exists() and self.needs_venv_recreation():
            print(f"Removing existing virtual environment at {self.venv_dir}")
            import shutil
            shutil.rmtree(self.venv_dir)
        
        if not self.venv_dir.exists():
            print(f"Creating virtual environment at {self.venv_dir}")
            venv.create(self.venv_dir, with_pip=True)
    
    def configure_pip(self):
        """Configure pip to suppress version check warnings"""
        if sys.platform == "win32":
            pip_config = self.venv_dir / "pip.ini"
        else:
            pip_config = self.venv_dir / "pip.conf"
        
        pip_config.write_text("[global]\ndisable-pip-version-check = true\n")
    
    def activate_venv_and_import_pip(self):
        """Activate venv by modifying sys.path and import pip"""
        # Add venv site-packages to sys.path
        if sys.platform == "win32":
            site_packages = self.venv_dir / "Lib" / "site-packages"
        else:
            python_version = f"python{sys.version_info.major}.{sys.version_info.minor}"
            site_packages = self.venv_dir / "lib" / python_version / "site-packages"
        
        # Insert at the beginning so venv packages take precedence
        sys.path.insert(0, str(site_packages))
        
        # Now import pip from the venv
        try:
            import pip
            return pip
        except ImportError:
            raise RuntimeError("Failed to import pip from virtual environment")
    
    def install_dependencies(self):
        """Install dependencies from requirements.txt"""
        if not self.requirements_file.exists():
            print(f"Warning: Requirements file not found: {self.requirements_file}", file=sys.stderr)
            return
        
        print("Installing dependencies...")
        
        # Use pip's main function directly
        from pip._internal import main as pip_main
        
        args = [
            "install",
            "-r", str(self.requirements_file),
            "--quiet",
            "--retries", "0"
        ]
        
        try:
            # pip_main returns 0 on success
            pip_main(args)
        except SystemExit as e:
            # pip calls sys.exit(), catch it
            if e.code != 0:
                print(f"Warning: Failed to install some dependencies (this may be okay if offline)", file=sys.stderr)
        except Exception as e:
            print(f"Warning: pip install failed: {e}", file=sys.stderr)
    
    def uninstall_tempo(self):
        """Uninstall existing tempo package if present"""
        from pip._internal import main as pip_main
        from pip._internal.commands.show import search_packages_info
        
        # Check if tempo is installed
        try:
            packages = list(search_packages_info(["tempo"]))
            if packages:
                print("Uninstalling previous tempo package...")
                try:
                    pip_main(["uninstall", "tempo", "--yes", "--quiet"])
                except SystemExit:
                    pass  # pip calls sys.exit(), ignore it
        except Exception:
            # Package not found or other error, that's fine
            pass
    
    def generate_api(self):
        """Generate the API using gen_api.py"""
        # Import and call gen_api directly
        gen_api_module_path = self.plugin_root / "Content/Python"
        
        if str(gen_api_module_path) not in sys.path:
            sys.path.insert(0, str(gen_api_module_path))
        
        try:
            import generate_tempo_api
        except Exception as e:
            print(f"Warning: Generate Tempo API failed: {e}", file=sys.stderr)
    
    def install_tempo_api(self):
        """Install the Tempo API package to the virtual environment"""
        if not self.api_dir.exists():
            raise RuntimeError(f"API directory not found: {self.api_dir}")
        
        setup_py = self.api_dir / "setup.py"
        if not setup_py.exists():
            raise RuntimeError(f"setup.py not found: {setup_py}")
        
        print("Installing Tempo API...")
        
        from pip._internal import main as pip_main
        
        args = ["install", str(self.api_dir), "--quiet", "--retries", "0"]
        
        try:
            pip_main(args)
        except SystemExit as e:
            if e.code != 0:
                print(f"Warning: Failed to install Tempo API (this may be okay if offline)", file=sys.stderr)
        except Exception as e:
            print(f"Warning: pip install failed: {e}", file=sys.stderr)
    
    def run(self):
        """Main execution flow"""
        print("Generating Python API...")
        
        try:
            # Create/verify virtual environment
            self.create_venv()
            
            # Configure pip
            self.configure_pip()
            
            # Activate venv and import pip
            self.activate_venv_and_import_pip()
            
            # Install dependencies
            self.install_dependencies()
            
            # Uninstall old tempo if present
            self.uninstall_tempo()
            
            # Generate the API
            self.generate_api()
            
            # Install the Tempo API
            self.install_tempo_api()
            
            print("Done")
            return 0
            
        except Exception as e:
            print(f"Error: {e}", file=sys.stderr)
            return 1


def main():
    parser = argparse.ArgumentParser(description="Generate Python API for Tempo plugin")
    parser.add_argument("project_root", help="Project root directory")
    parser.add_argument("plugin_root", help="Plugin root directory")
    
    args = parser.parse_args()
    
    generator = APIGenerator(args)
    return generator.run()


if __name__ == "__main__":
    sys.exit(main())
