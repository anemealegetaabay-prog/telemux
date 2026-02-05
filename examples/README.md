# Examples

`sample.tlmx` is a small hand-built telemux stream (single unfragmented
frame, one channel group with two sample sections) useful for exercising
the CLI's `decode`/`stats` subcommands during development.

```
telemux_cli decode examples/sample.tlmx
telemux_cli stats
```
