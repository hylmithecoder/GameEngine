---
description: GDB debugger cheat sheet for deep debugging C++ applications
---

# GDB Cheat Sheet

## Starting GDB

```bash
# Start with executable
gdb ./ViewPort3D

# Start with core dump
gdb ./ViewPort3D core

# Attach to running process
gdb -p <PID>
```

---

## Running & Controlling Execution

| Command | Short | Description |
|---------|-------|-------------|
| `run` | `r` | Start program |
| `run arg1 arg2` | | Run with arguments |
| `continue` | `c` | Continue after breakpoint |
| `next` | `n` | Step over (don't enter functions) |
| `step` | `s` | Step into functions |
| `finish` | `fin` | Run until current function returns |
| `until <line>` | `u` | Run until line number |
| `kill` | | Kill running program |
| `quit` | `q` | Exit GDB |

---

## Breakpoints

| Command | Description |
|---------|-------------|
| `break main` | Break at function `main` |
| `break filename.cpp:42` | Break at line 42 |
| `break MyClass::method` | Break at class method |
| `break *0x400500` | Break at memory address |
| `tbreak <location>` | Temporary breakpoint (auto-delete) |
| `info breakpoints` | List all breakpoints |
| `delete <num>` | Delete breakpoint by number |
| `delete` | Delete all breakpoints |
| `disable <num>` | Disable breakpoint |
| `enable <num>` | Enable breakpoint |
| `clear <location>` | Clear breakpoint at location |

### Conditional Breakpoints
```gdb
break myfile.cpp:50 if x == 5
break myFunc if strcmp(name, "test") == 0
condition 1 x > 10    # Add condition to bp #1
```

---

## Exception Handling (CRITICAL FOR DEBUGGING!)

```gdb
# Catch ALL C++ exceptions when thrown
catch throw

# Catch ALL C++ exceptions when caught
catch catch

# Catch specific exception type
catch throw std::runtime_error

# Catch signals
catch signal SIGSEGV
catch signal SIGFPE
catch signal SIGABRT

# Continue after catching
continue
```

### Common Signals
| Signal | Description |
|--------|-------------|
| `SIGSEGV` | Segmentation fault (invalid memory access) |
| `SIGABRT` | Abort signal (assert failed, abort() called) |
| `SIGFPE` | Floating point exception (divide by zero) |
| `SIGBUS` | Bus error (memory alignment) |

---

## Stack & Backtrace

| Command | Short | Description |
|---------|-------|-------------|
| `backtrace` | `bt` | Show call stack |
| `backtrace full` | `bt full` | Show stack with local variables |
| `backtrace 10` | `bt 10` | Show last 10 frames |
| `frame <num>` | `f <num>` | Select stack frame |
| `up` | | Move up one frame |
| `down` | | Move down one frame |
| `info frame` | | Info about current frame |
| `info args` | | Function arguments |
| `info locals` | | Local variables |

---

## Examining Variables

| Command | Description |
|---------|-------------|
| `print var` | Print variable value |
| `print *ptr` | Print dereferenced pointer |
| `print arr[0]@10` | Print 10 elements of array |
| `print/x var` | Print as hex |
| `print/t var` | Print as binary |
| `print/d var` | Print as decimal |
| `print/c var` | Print as character |
| `print sizeof(var)` | Print size |
| `ptype var` | Print variable type |
| `whatis var` | Brief type info |
| `display var` | Auto-print var at each stop |
| `undisplay <num>` | Remove auto-display |

### Format Specifiers
- `/x` - hex
- `/d` - decimal
- `/t` - binary
- `/o` - octal
- `/c` - character
- `/s` - string
- `/f` - float

---

## Memory Examination

```gdb
# Examine memory: x/nfu address
# n = count, f = format, u = unit size

x/10x $rsp          # 10 hex words at stack pointer
x/20i $pc           # 20 instructions at program counter
x/s str_ptr         # String at pointer
x/10b ptr           # 10 bytes at ptr
x/4gx ptr           # 4 giant (8-byte) words in hex
```

### Unit Sizes
- `b` - bytes
- `h` - halfwords (2 bytes)
- `w` - words (4 bytes)
- `g` - giant (8 bytes)

---

## Watchpoints (Data Breakpoints)

```gdb
# Break when variable changes
watch myVar

# Break when memory location changes
watch *0x7fffffffdc10

# Break on read
rwatch myVar

# Break on read or write
awatch myVar

# List watchpoints
info watchpoints
```

---

## Threads

| Command | Description |
|---------|-------------|
| `info threads` | List all threads |
| `thread <num>` | Switch to thread |
| `thread apply all bt` | Backtrace for all threads |
| `set scheduler-locking on` | Lock to current thread |

---

## Source Code

| Command | Description |
|---------|-------------|
| `list` | Show source around current line |
| `list <function>` | Show function source |
| `list <file>:<line>` | Show source at line |
| `list -` | Show previous lines |
| `set listsize 30` | Show 30 lines per list |
| `directory <path>` | Add source search path |

---

## Useful GDB Settings

```gdb
# Pretty print STL containers (vectors, maps, etc.)
set print pretty on

# Print array indices
set print array-indexes on

# Don't stop on SIGPIPE (common in socket programming)
handle SIGPIPE nostop noprint

# Pagination off (no "press enter to continue")
set pagination off

# Save command history
set history save on
set history filename ~/.gdb_history

# Demangle C++ names
set print demangle on
set print asm-demangle on
```

---

## Vulkan/Graphics Debugging Tips

```gdb
# Common places to set breakpoints in Vulkan
break vkCreateInstance
break vkCreateDevice
break vkQueueSubmit

# SDL3 breakpoints
break SDL_CreateWindow
break SDL_CreateRenderer

# ImGui breakpoints
break ImGui::Render
break ImGui_ImplVulkan_Init

# Your engine breakpoints
break ViewPort3D::init
break VulkanHandler::CreateDevice
```

---

## Quick Debug Session Example

```gdb
# Start debugging
gdb ./ViewPort3D

# Inside GDB:
(gdb) catch throw                      # Catch all exceptions
(gdb) break main                       # Break at main
(gdb) run                              # Start program

# When exception occurs:
(gdb) bt                               # See where it happened
(gdb) frame 0                          # Go to the crash frame
(gdb) info locals                      # See local variables
(gdb) print myVar                      # Examine specific variable
(gdb) list                             # See source code
```

---

## TUI Mode (Text User Interface)

```gdb
# Enable TUI mode
tui enable
# or start gdb with: gdb -tui ./program

# Layout options
layout src        # Source only
layout asm        # Assembly only
layout split      # Source + Assembly
layout regs       # + Registers

# Navigate
focus src         # Focus source window
focus cmd         # Focus command window
```

---

## Save/Restore Session

```gdb
# Save breakpoints to file
save breakpoints bp.txt

# Load breakpoints from file
source bp.txt

# Save all settings
save gdb-add-index

# Run GDB commands from file
source commands.gdb
```