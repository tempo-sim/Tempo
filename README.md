# VayuSim
The VayuSim project built on top of Tempo.

## Getting Started
Make sure you have a valid GitHub Personal Access Token for the TempoThirdParty repo in your `~/.netrc` file. The format should be like this:
```
machine api.github.com
login user # Can be anything. Not used, but must be present.
password <your_token_here>
```
Run `Setup.sh` (from the Tempo root) once to sync third party dependencies and add git hooks to keep them in sync automatically.
