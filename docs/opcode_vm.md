# Transform pipeline opcodes

Decoded sample data can optionally be post-processed by a small
data-driven instruction stream before being handed to a consumer. Each
instruction operates on numbered registers that reference bytes held by a
`SampleArena`.

| Opcode | Fields used | Description |
|---|---|---|
| `FILTER` | `reg_a`, `param` | Scales every byte in `reg_a` by a Q8 fixed-point gain |
| `RESAMPLE` | `reg_a`, `param` | Subsamples `reg_a` in place by a stride |
| `MERGE_CHANNEL` | `reg_a`, `reg_b`, `reg_dst` | Concatenates `reg_a` and `reg_b` into `reg_dst` |
| `SNAPSHOT` | `reg_a`, `slot` | Captures `reg_a`'s current contents into undo slot `slot` |
| `ROLLBACK` | `slot` | Restores the register captured by undo slot `slot` |
| `RESERVE` | `reg_dst`, `param` | Stages `param` fixed-width records and binds the packed block to `reg_dst` |

`SNAPSHOT`/`ROLLBACK` let a program back out a filter pass that made a
channel worse without needing to re-decode it. Both are zero-copy: they
reference the arena's storage directly rather than duplicating bytes, so a
program can call `SNAPSHOT` many times per invocation without added
allocation.
