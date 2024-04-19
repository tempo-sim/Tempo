# Tempo
The Tempo Unreal Engine project and plugins

## Getting Started
Make sure you have a valid GitHub Personal Access Token for the TempoThirdParty repo in your `~/.netrc` file. The format should be like this:
```
machine api.github.com
login user # Can be anything. Not used, but must be present.
password <your_token_here>
```
Run `Setup.sh` (from the Tempo root) once to:
  - Install the Tempo UnrealBuildTool toolchain for your system and rebuild UnrealBuildTool
  - Install third party dependencies and add git hooks to keep them in sync automatically
