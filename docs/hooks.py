# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# MkDocs build hooks for the Tempo documentation site.

# The generated gRPC reference's nav file. mkdocs-literate-nav reads it to build
# that section's navigation; it is not a page anyone should land on or find in
# search. Hooks run after plugins, so removing it here is safe - literate-nav has
# already consumed it. `exclude_docs` cannot do this job: it filters the docs
# directory, and this file is produced by mkdocs-gen-files at build time.
_GENERATED_NAV_FILE = "reference/api/SUMMARY.md"


def on_files(files, config):
    for file in list(files):
        if file.src_uri == _GENERATED_NAV_FILE:
            files.remove(file)
    return files
