# Tempo C++ client tests

A CMake project against the packaged C++ client (`Packaged/API/Cpp`, written by
`Scripts/Package.sh` with `TEMPO_GEN_CPP_API=1`), one executable per group:

| Group | Needs a sim | What |
|---|---|---|
| `contract` | no | the client links (RPC client, reflected API, shared `tempogrpc`), the conversions, the typed hierarchy |
| `integration` | yes | the RPC client against a running sim |
| `typed` | yes | the reflected API, on engine and Tempo classes every build has |

```bash
Scripts/TestCppAPI.sh contract
Scripts/TestCppAPI.sh typed
```

The runner builds the group, launches the packaged sim for the groups that need one, and writes its
logs to `Saved/CppTestReport` (or `TEMPO_TEST_REPORT_DIR`). The harness is `harness.h`: cases
register themselves with `TEMPO_TEST`, `CHECK` records a failure and goes on, `REQUIRE` and
`REQUIRE_OK` leave the case. An argument to an executable runs only the cases whose names contain it.
