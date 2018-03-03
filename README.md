# daemon

A *nix daemon template/example, complete with the latest in desirable daemon features, including:
 - forking to the background
 - logging to syslog
 - parsing command-line arguments
 - parsing a simplistic config file

All written in plain old C99. Half-tested on Linux with GCC.

# Usage
All core daemon functionality is in the `daemon_main` function, just above `main`. Any daemon options which might, for example, be specified on the command-line or in a configuration file belong in the `options_t` structure, which is populated by the `parse_config_file` and the `parse_cmdline_opts` functions. Simple configuration files of the following format are also supported:
```
# This is a comment. Everything after the #, up to the end of the line, is ignored.
SomeParameter=SomeValue
# Parameter names may start with any [a-zA-Z_] character, and the rest of the parameter
# may be any [a-zA-Z0-9_-] character, for example
_An0ther_-_Param3t3r = false
# Parameter values may be enclosed in quotation marks, in which case comments are ignored:
AThirdParameter = "Some Other value # this is not a comment
which spans multiple lines"
# To include quotation marks in a quoted parameter value, escape it: \"
```

# License
This code is released under the Boost v1 license - see the header at the start of `daemon.c` for more information.
The Boost license is [GPL Compatible](https://www.gnu.org/licenses/license-list.en.html#boost). This means you can release software based on this template under the GPL, as long as you retain copyright information and the original Boost license header.
