## Commit style

When you create git commits, the commit message must contain ONLY the engineering
message. Do not append "🤖 Generated with Claude Code", "Co-Authored-By: Claude",
or any other attribution footer. Plain commits only.

## Hard rules for this repo

- Never edit src/server.cpp, src/client_naive.cpp, src/json.hpp, or
  bench/correctness.cpp.
- Every committed change must satisfy ./bin/correctness printing "PASS".
- Never install packages or hit the public internet.
