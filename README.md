# LinuxManager - *The Easiest Task Manager for Linux*

An **extremely compact** native C/Xlib system monitor for Linux desktops running X11 or XWayland. It is designed to be **smaller and lighter than the original 1996 Windows Task Manager**, with an executable size of approximately **77 KB** and a RAM footprint of around **8 MB** under testing.
<br><br>
<sub>For comparison, this entire `README` is about **a quarter** of the size of the **compiled executable**; *and it's just text*!</sub>

Compared to the newer 4 MB Windows Task Manager, **LinuxManager** aims to deliver an even better design and essential system-management features in a dramatically smaller package, **while also being completely open-source**.
## Install

You can run **LinuxManager** directly by downloading the latest precompiled executable from `Releases`, or build and install it from source by following the instructions below.

1. Download the project as a `.zip` file.
2. Extract the files into your desired location.
3. Install a *C compiler*, *make*, and the *required X11/Xft development libraries* for your distribution.
4. Right-click `install-app.sh` → **Properties → Permissions** → enable **Is executable** if needed.
5. Double-click `install-app.sh` and choose **Execute** when asked.
6. Search for **Task Manager** in the application launcher and run it.

## Uninstall (When Installed from Source)

1. Right-click `uninstall-app.sh` → **Properties → Permissions** → enable **Is executable** if needed.
2. Double-click `uninstall-app.sh` and choose **Execute** when asked. It removes the installed *binary*, *launcher entry* etc. from your system. It does not delete personal files or stop/change system services.

## Build

Run the following command inside your **LinuxManager** project folder to compile the source code and create the `taskmgr` executable. This builds the application but does not install it on your system.

```sh
chmod +x build.sh
./build.sh
```

The executable is dynamically linked to *X11*, *Xft*, *XRender*, *libc* and *requires X11/XWayland* plus those runtime libraries at runtime. The build script checks that the executable is below 1,000,000 bytes.
<br>
<sub>**Note:** When compiled from source, the executable is approximately **100 KB** in size.</sub>

## Benchmarking & Testing

**LinuxManager**'s executable size and memory usage are measured by testing the compiled application in a *live desktop environment*.

- **Executable Size:** Measured in bytes using the compiled `taskmgr` binary.
- **RAM Usage:** Measured while the application is running, using its resident memory (RSS) and proportional set size (PSS).
- **Test Environment:** *Fedora Linux* with *KDE Plasma*, running on X11/XWayland-compatible infrastructure.
- **Comparison:** File sizes and memory measurements are compared separately, since executable size on disk does not represent RAM usage.

The current build has an executable size of approximately **77 KB** and has used around **8 MB of RAM** in a previous test. Actual memory usage varies *depending on the system*, *ibraries*, *desktop environment*, and *workload*. These figures are measured results, **not guaranteed values**!

## Pages and working features

- **Search across long lists:** the same clickable search field is available on App history, Startup apps, Users, Details, Services, Disk & Network, GPU, System info, CPU details, Memory details, and Network, in addition to Processes. Queries filter the actual displayed rows/metrics; click the field to focus it, type anywhere on a searchable page to start filtering, and click elsewhere to return keyboard focus to the page. Search scrolling resets to the top when the query changes.
- **Appearance:** bottom-left button switches between light and dark palettes and saves the selection in `~/.config/wintask-linux/theme`.

- **Processes:** live process table, alphabetic default sort, PID/user/state/CPU/RSS, three-decimal short-term-smoothed CPU estimates (any sampled value below 0.001%, including zero, displays as `<0.001%` rather than `0.000%`), filtering, column sorting, wheel scrolling, F5 refresh, Delete to end selected process, and Run new task. The search prompt says “Click or type to search”; clicking focuses it and starting to type on the Processes page also activates it. The Run dialog uses a translucent dimmed backdrop with a darker header, aligned Run/Cancel buttons, up to ten visible suggestion rows, alphabetized commands from common apps and executables found in `PATH`, prefix-first matching, a visible scrollbar, mouse-wheel scrolling, Up/Down/Tab selection, and Page Up/Page Down navigation. Its command field is focused immediately; click any suggestion to fill the field or press Enter to launch the highlighted command.
- **Context menus:** menus are generated from the current item state. Right-click a live process for end/force-stop, state-aware suspend or resume, priority changes, executable folder/path, web search, copy PID (needs `xclip`), and properties; zombie processes omit actions that cannot work for an exited task. Startup entries offer Enable or Disable, never both. Service menus offer Start or Stop based on active state, not both at once.
- **Performance:** live CPU and memory history graphs, RAM use, logical CPUs, uptime, and process count.
- **App history:** session-only average CPU and peak RSS by process name. Rows support click selection, Up/Down keyboard selection, and independent wheel scrolling.
- **Startup apps:** XDG autostart entries, user-owned enable/disable, and per-user `Hidden=true` overrides for system-wide entries (without editing `/etc` or `/usr/share`). Existing user overrides are backed up and restored when toggling. Toggle actions reload the list immediately and show amber processing feedback plus a success/error result. Row clicks are restricted to the list itself so sidebar navigation remains clickable.
- **Users:** local account names, UIDs, home directories, and shells.
- **Details:** selected process's parent PID, state, CPU estimate, resident and virtual memory, thread count, executable path, and command line. Select a process on the Processes page first.
- **Services:** systemd service units with state-aware right-click menus. Active services offer Stop / Restart / Status; inactive or failed services offer Start / Status; transitioning units show only the applicable action, never Start and Stop together. Operations show an amber Processing indicator and are not reported as successful just because `systemctl` returned zero: the app polls `systemctl show` for up to 15 seconds and only reports success after the requested state is observed (including successful one-shot services). A command error, unreadable state, failed state, or timeout produces a clear failure message. Right-click uses the latest service list; selection is tracked by unit name rather than a volatile list index. Row clicks are restricted to the actual table region, and clicking outside a context menu closes it without swallowing the click. Authorization may be required.
- **Disk & Network:** block-device I/O and cumulative interface byte counters.
- **GPU:** detected DRM graphics devices and PCI vendor IDs where exposed.
- **System info:** distribution, kernel, architecture, hostname, CPU model, online CPU count, page size, uptime, process count, RAM, root filesystem capacity, boot ID, shell, UID, home, display server, and process limit.
- **CPU details:** per-logical-CPU kernel tick counters with separate scrolling. The displayed counters naturally increase as the kernel accumulates time; scrolling changes only the visible cores.
- **Memory details:** selected physical-memory, swap, and virtual-memory counters from `/proc/meminfo`, with labels and values clipped within their panel bounds.
- **Network:** per-interface received/transmitted bytes and packet counts, with a separate header area and scrollable rows. CPU/network detail pages keep sidebar navigation clickable.

## Important limitations

This is not yet feature-for-feature parity with Windows Task Manager. Linux does not provide one universal interface for all GPU utilization/VRAM/temperature values; support depends on the GPU driver. Per-process disk/network rates, efficiency mode, energy impact, persistent app history, process-tree controls, and some Windows-specific app management features are not implemented yet. Process access and service actions are limited by Linux permissions. The GUI is an X11/XWayland client rather than a native Wayland client. Action feedback appears in multiple places: an orange processing badge in the top bar, an amber footer status, and an amber highlight on the active row where applicable. Instantaneous actions keep the processing indicator visible for at least 850 ms; afterward a green completion or red failure badge and message remain visible briefly. Process context actions, startup toggles/path actions, and systemd service operations report progress/results.


### Protected process termination

For End task and Force stop, the app first sends the signal with the current user permissions. If Linux reports `EPERM`, it requests elevated authorization for only that PID and signal through `pkexec` (with `sudo -A` as a fallback). The application itself is not run as root. A PolicyKit authentication agent or configured sudo askpass is required for the prompt to work. A process may still be impossible to stop if it has exited, the request is not authorized, or it is stuck in an uninterruptible kernel wait.
