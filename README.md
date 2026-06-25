![terminal-logos](https://github.com/microsoft/terminal/assets/91625426/333ddc76-8ab2-4eb4-a8c0-4d7b953b1179)

[![Terminal Build Status](https://dev.azure.com/shine-oss/terminal/_apis/build/status%2FTerminal%20CI?branchName=main)](https://dev.azure.com/shine-oss/terminal/_build/latest?definitionId=1&branchName=main)

---

## 📌 About this fork

> 🌐 **日本語版はこちら → [README.ja.md](./README.ja.md)** (Japanese)

This is a **personal fork** of [microsoft/terminal](https://github.com/microsoft/terminal), maintained for private use. It is **not** affiliated with or endorsed by Microsoft, and is **not** an official build. The original project is licensed under the [MIT License](./LICENSE), which is preserved here.

> Based on Windows Terminal **v1.24.2372.0**.

### Custom changes in this fork

This fork adds a few pane-management conveniences, several new pane types
(file browser, web browser, winget), and read-only pane sharing over a local
WebSocket, on top of upstream.

#### 1. Equalize panes

Instantly resizes every split in the current tab so all panes end up with an
equal area — a clean, tiling-window-manager-style "equalize" command. Each
split is weighted by how many panes are on either side, so every pane (not just
each half) gets the same size.

- **Action:** `equalizePanes` (id `Terminal.EqualizePanes`)
- No-op when the tab has only a single pane.

#### 2. Mouse drag-to-resize panes

A thin, hit-testable strip sits on each split boundary. Drag it with the mouse
to resize the two adjacent panes live (clamped to their minimum sizes). The
strip is invisible at rest and faintly highlights on hover. Upstream Windows
Terminal only supports resizing panes with the keyboard (`Alt+Shift+arrows`).

#### 3. Mouse drag-to-move panes (`Alt`+drag)

Hold **`Alt`** and drag a pane onto another pane to move it there. While
dragging, the picked-up pane is tinted and a translucent, pane-sized ghost
follows the cursor. On drop, the target pane is split along its longer axis and
the moved pane is inserted on the half (top/bottom or left/right) you released
over. Releasing outside the pane area cancels the move.

> Because `Alt`+left-drag is repurposed for moving panes, the terminal's
> `Alt`+drag **block (rectangular) selection** is not available via that gesture
> in this fork.

#### 4. File browser pane

A keyboard-driven file browser and viewer hosted as a pane. Move with the arrow
keys, `Enter` to open a folder, `Backspace` to go up, and `D` for the drive
list. Selecting a file previews it — text (UTF-8/UTF-16, with BOM handling) in a
viewer, images (`png`/`jpg`/`gif`/`bmp`/`ico`/`tiff`/`webp`) rendered inline.
You can also drag files or folders from Explorer onto the pane.

- **Action:** `openFileBrowser` (id `Terminal.OpenFileBrowser`)
- **Action:** `selectFileBrowserDrive` (id `Terminal.SelectFileBrowserDrive`) — show the drive list

#### 5. Web browser pane

A [WebView2](https://learn.microsoft.com/microsoft-edge/webview2/) (Chromium/Edge)
browser hosted as a pane: an address bar with back / forward / reload and a
paste-from-clipboard button. Type a URL, or a search term to send it to your
configured search engine. The page shown on open is the `browserHomePage`
global setting.

- **Action:** `openBrowserPane` (id `Terminal.OpenBrowserPane`)
- **Setting:** `browserHomePage` (global; default `https://www.google.com`)
- Requires the WebView2 Runtime (ships with Microsoft Edge). The portable
  distribution bundles the required `WebView2Loader.dll` and
  `Microsoft.Web.WebView2.Core.dll` automatically.

#### 6. winget pane

A small GUI front-end for the [Windows Package Manager](https://github.com/microsoft/winget-cli)
(`winget`): search packages, list installed / upgradable, and
Install / Upgrade / Upgrade All / Uninstall from the list. It runs `winget.exe`
under the hood, so Install/Upgrade may prompt for elevation (UAC). At most one
winget pane exists at a time; opening it again focuses the existing one.

- **Action:** `openWingetPane` (id `Terminal.OpenWingetPane`)
- Requires `winget` (Windows Package Manager) to be installed (it ships with
  current Windows 10/11).

#### 7. Pane sharing (read-only)

Share a live terminal pane so it can be watched from another pane, window, or
machine — read-only. `sharePane` starts a small WebSocket server bound to
**localhost** and copies a `ws://localhost:<port>/?token=…` URL (with a random
token) to the clipboard. `connectSharedSession` opens a viewer pane: it
pre-fills that URL from the clipboard, connects, and mirrors the host pane live.
A viewer that joins late immediately sees the current screen (the host sends a
VT snapshot of its buffer), then live output. The tab of a shared pane shows a
share glyph; `stopSharePane` ends the share and clears it.

The sharing engine lives in `src/cascadia/SharingEngine` and is independent of
Windows Terminal (its protocol / WebSocket / authorization core builds and
unit-tests on its own). The host never modifies its `ConptyConnection`; it tees
the existing connection's output, so sharing can't change what the shell does.

Security model (read-only means *full disclosure*, not *safe* — anyone with the
URL and token sees everything on that pane):

- **Localhost only by default.** The server binds loopback. Plaintext `ws://` is
  never meant to leave the machine directly — expose it (if at all) only behind a
  TLS-terminating reverse proxy.
- **Read-only is enforced on the server**, not by trusting the viewer (a viewer's
  input frames are dropped).
- **Nothing is sent before the token is verified**; a bad or expired token gets
  only a rejection.
- **Auto-expiry.** The token only admits new viewers for 30 minutes, and a share
  with no viewers auto-stops after 10 minutes, so a forgotten share doesn't keep
  a shell exposed.

##### Connecting from another machine

Two ways, depending on how much you trust the network:

- **Recommended — an SSH tunnel (encrypted + authenticated).** Keep the host on
  the default localhost share (`sharePane`). On the viewer machine, run
  `connectSharedSessionViaSsh`: enter the **SSH target** (`user@host`) and paste
  the host's `ws://localhost:<port>/?token=…` URL. Windows Terminal launches the
  tunnel for you — it runs `ssh -N -L <local>:localhost:<port> user@host` in the
  background, connects the viewer to `ws://localhost:<local>/?token=…`, and kills
  the `ssh` process when the viewer pane closes. Nothing but SSH crosses the
  network. Requirements: an OpenSSH **server** running on the host, and
  **key/agent auth** on the viewer (the tunnel runs with `BatchMode=yes`, so a
  password prompt would just fail — set up public keys once). First-time host keys
  are accepted with `StrictHostKeyChecking=accept-new`. A mesh VPN such as
  Tailscale works equally well (then plaintext `ws://` rides the encrypted
  tailnet, and you can share straight to the VPN address).
- **Plaintext on a trusted LAN (`sharePaneOnLan`).** Binds every interface and
  builds the URL with this machine's LAN IP, so other PCs can connect directly.
  This is **plaintext over the network** (token-only protection) — use it only on
  a network you trust. The dialog warns you, and you may need to allow Windows
  Terminal through Windows Firewall on Private networks. Prefer the SSH tunnel
  whenever the channel isn't fully trusted.

- **Action:** `sharePane` (id `Terminal.SharePane`) — share the focused pane (localhost)
- **Action:** `sharePaneOnLan` (id `Terminal.SharePaneOnLan`) — share it on the LAN (plaintext; opt-in)
- **Action:** `stopSharePane` (id `Terminal.StopSharePane`) — stop sharing it
- **Action:** `connectSharedSession` (id `Terminal.ConnectSharedSession`) — open a viewer pane
- **Action:** `connectSharedSessionViaSsh` (id `Terminal.ConnectSharedSessionViaSsh`) — open a viewer over an auto-launched SSH tunnel

#### 8. Dedicated `keybindings.json`

Key bindings can be managed in a separate `keybindings.json` file in the app's
`LocalState` folder. Unlike settings *fragments*, this file is allowed to define
key bindings, and it is layered **on top of** `settings.json` (so it takes
precedence). Edit it directly to keep your key bindings separate from the rest
of your settings.

#### Keybindings

Drag-to-resize and `Alt`+drag-to-move are mouse gestures and need no key
binding. The new keyboard actions below are **not bound to any key by default** —
bind the ones you want in `settings.json` (or via the Settings UI → Actions):

```jsonc
{
    "keybindings": [
        { "id": "Terminal.EqualizePanes", "keys": "ctrl+alt+e" },
        { "id": "Terminal.OpenFileBrowser", "keys": "alt+f" },
        { "id": "Terminal.OpenBrowserPane", "keys": "alt+b" },
        { "id": "Terminal.OpenWingetPane", "keys": "alt+w" },
        { "id": "Terminal.SharePane", "keys": "alt+s" },
        { "id": "Terminal.SharePaneOnLan", "keys": "alt+shift+l" },
        { "id": "Terminal.StopSharePane", "keys": "alt+shift+s" },
        { "id": "Terminal.ConnectSharedSession", "keys": "alt+shift+c" },
        { "id": "Terminal.ConnectSharedSessionViaSsh", "keys": "alt+shift+t" }
    ]
}
```

| Action ID | Suggested keys | What it does |
|-----------|----------------|--------------|
| `Terminal.EqualizePanes` | `ctrl+alt+e` | Make all panes in the current tab equal-sized |
| `Terminal.OpenFileBrowser` | `alt+f` | Open the keyboard file browser pane |
| `Terminal.SelectFileBrowserDrive` | *(unbound)* | Show the drive list in the focused file browser |
| `Terminal.OpenBrowserPane` | `alt+b` | Open a WebView2 web browser pane |
| `Terminal.OpenWingetPane` | `alt+w` | Open the winget package-manager pane |
| `Terminal.SharePane` | `alt+s` | Share the focused pane read-only (localhost) |
| `Terminal.SharePaneOnLan` | `alt+shift+l` | Share the focused pane on the LAN (plaintext; opt-in) |
| `Terminal.StopSharePane` | `alt+shift+s` | Stop sharing the focused pane |
| `Terminal.ConnectSharedSession` | `alt+shift+c` | Open a viewer pane for a shared session |
| `Terminal.ConnectSharedSessionViaSsh` | `alt+shift+t` | Open a viewer over an auto-launched SSH tunnel |

---

Everything below this section is the original Microsoft README, kept for reference.

---

# Welcome to the Windows Terminal, Console and Command-Line repo

<details>
  <summary><strong>Table of Contents</strong></summary>

- [Installing and running Windows Terminal](#installing-and-running-windows-terminal)
  - [Microsoft Store \[Recommended\]](#microsoft-store-recommended)
  - [Other install methods](#other-install-methods)
    - [Via GitHub](#via-github)
    - [Via Windows Package Manager CLI (aka winget)](#via-windows-package-manager-cli-aka-winget)
    - [Via Chocolatey (unofficial)](#via-chocolatey-unofficial)
    - [Via Scoop (unofficial)](#via-scoop-unofficial)
- [Installing Windows Terminal Canary](#installing-windows-terminal-canary)
- [Terminal \& Console Overview](#terminal--console-overview)
  - [Windows Terminal](#windows-terminal)
  - [The Windows Console Host](#the-windows-console-host)
  - [Shared Components](#shared-components)
  - [Creating the new Windows Terminal](#creating-the-new-windows-terminal)
- [Resources](#resources)
- [FAQ](#faq)
  - [I built and ran the new Terminal, but it looks just like the old console](#i-built-and-ran-the-new-terminal-but-it-looks-just-like-the-old-console)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [Communicating with the Team](#communicating-with-the-team)
- [Developer Guidance](#developer-guidance)
- [Prerequisites](#prerequisites)
- [Building the Code](#building-the-code)
  - [Building in PowerShell](#building-in-powershell)
  - [Building in Cmd](#building-in-cmd)
- [Running \& Debugging](#running--debugging)
  - [Coding Guidance](#coding-guidance)
- [Code of Conduct](#code-of-conduct)

</details>

<br />

This repository contains the source code for:

* [Windows Terminal](https://aka.ms/terminal)
* [Windows Terminal Preview](https://aka.ms/terminal-preview)
* The Windows console host (`conhost.exe`)
* Components shared between the two projects
* [ColorTool](./src/tools/ColorTool)
* [Sample projects](./samples)
  that show how to consume the Windows Console APIs

Related repositories include:

* [Windows Terminal Documentation](https://docs.microsoft.com/windows/terminal)
  ([Repo: Contribute to the docs](https://github.com/MicrosoftDocs/terminal))
* [Console API Documentation](https://github.com/MicrosoftDocs/Console-Docs)
* [Cascadia Code Font](https://github.com/Microsoft/Cascadia-Code)

## Installing and running Windows Terminal

> [!NOTE]
> Windows Terminal requires Windows 10 2004 (build 19041) or later

### Microsoft Store [Recommended]

Install the [Windows Terminal from the Microsoft Store][store-install-link].
This allows you to always be on the latest version when we release new builds
with automatic upgrades.

This is our preferred method.

### Other install methods

#### Via GitHub

For users who are unable to install Windows Terminal from the Microsoft Store,
released builds can be manually downloaded from this repository's [Releases
page](https://github.com/microsoft/terminal/releases).

Download the `Microsoft.WindowsTerminal_<versionNumber>.msixbundle` file from
the **Assets** section. To install the app, you can simply double-click on the
`.msixbundle` file, and the app installer should automatically run. If that
fails for any reason, you can try the following command at a PowerShell prompt:

```powershell
# NOTE: If you are using PowerShell 7+, please run
# Import-Module Appx -UseWindowsPowerShell
# before using Add-AppxPackage.

Add-AppxPackage Microsoft.WindowsTerminal_<versionNumber>.msixbundle
```

> [!NOTE]
> If you install Terminal manually:
>
> * You may need to install the [VC++ v14 Desktop Framework Package](https://docs.microsoft.com/troubleshoot/cpp/c-runtime-packages-desktop-bridge#how-to-install-and-update-desktop-framework-packages).
>   This should only be necessary on older builds of Windows 10 and only if you get an error about missing framework packages.
> * Terminal will not auto-update when new builds are released so you will need
>   to regularly install the latest Terminal release to receive all the latest
>   fixes and improvements!

#### Via Windows Package Manager CLI (aka winget)

[winget](https://github.com/microsoft/winget-cli) users can download and install
the latest Terminal release by installing the `Microsoft.WindowsTerminal`
package:

```powershell
winget install --id Microsoft.WindowsTerminal -e
```

> [!NOTE]
> Dependency support is available in WinGet version [1.6.2631 or later](https://github.com/microsoft/winget-cli/releases). To install the Terminal stable release 1.18 or later, please make sure you have the updated version of the WinGet client.

#### Via Chocolatey (unofficial)

[Chocolatey](https://chocolatey.org) users can download and install the latest
Terminal release by installing the `microsoft-windows-terminal` package:

```powershell
choco install microsoft-windows-terminal
```

To upgrade Windows Terminal using Chocolatey, run the following:

```powershell
choco upgrade microsoft-windows-terminal
```

If you have any issues when installing/upgrading the package please go to the
[Windows Terminal package
page](https://chocolatey.org/packages/microsoft-windows-terminal) and follow the
[Chocolatey triage process](https://chocolatey.org/docs/package-triage-process)

#### Via Scoop (unofficial)

[Scoop](https://scoop.sh) users can download and install the latest Terminal
release by installing the `windows-terminal` package:

```powershell
scoop bucket add extras
scoop install windows-terminal
```

To update Windows Terminal using Scoop, run the following:

```powershell
scoop update windows-terminal
```

If you have any issues when installing/updating the package, please search for
or report the same on the [issues
page](https://github.com/lukesampson/scoop-extras/issues) of Scoop Extras bucket
repository.

---

## Installing Windows Terminal Canary
Windows Terminal Canary is a nightly build of Windows Terminal. This build has the latest code from our `main` branch, giving you an opportunity to try features before they make it to Windows Terminal Preview.

Windows Terminal Canary is our least stable offering, so you may discover bugs before we have had a chance to find them.

Windows Terminal Canary is available as an App Installer distribution and a Portable ZIP distribution.

The App Installer distribution supports automatic updates. Due to platform limitations, this installer only works on Windows 11.

The Portable ZIP distribution is a portable application. It will not automatically update and will not automatically check for updates. This portable ZIP distribution works on Windows 10 (19041+) and Windows 11.

| Distribution  | Architecture    | Link                                                 |
|---------------|:---------------:|------------------------------------------------------|
| App Installer | x64, arm64, x86 | [Download](https://aka.ms/terminal-canary-installer) |
| Portable ZIP  | x64             | [Download](https://aka.ms/terminal-canary-zip-x64)   |
| Portable ZIP  | ARM64           | [Download](https://aka.ms/terminal-canary-zip-arm64) |
| Portable ZIP  | x86             | [Download](https://aka.ms/terminal-canary-zip-x86)   |

_Learn more about the [types of Windows Terminal distributions](https://learn.microsoft.com/windows/terminal/distributions)._

---

## Terminal & Console Overview

Please take a few minutes to review the overview below before diving into the
code:

### Windows Terminal

Windows Terminal is a new, modern, feature-rich, productive terminal application
for command-line users. It includes many of the features most frequently
requested by the Windows command-line community including support for tabs, rich
text, globalization, configurability, theming & styling, and more.

The Terminal will also need to meet our goals and measures to ensure it remains
fast and efficient, and doesn't consume vast amounts of memory or power.

### The Windows Console Host

The Windows Console host, `conhost.exe`, is Windows' original command-line user
experience. It also hosts Windows' command-line infrastructure and the Windows
Console API server, input engine, rendering engine, user preferences, etc. The
console host code in this repository is the actual source from which the
`conhost.exe` in Windows itself is built.

Since taking ownership of the Windows command-line in 2014, the team added
several new features to the Console, including background transparency,
line-based selection, support for [ANSI / Virtual Terminal
sequences](https://en.wikipedia.org/wiki/ANSI_escape_code), [24-bit
color](https://devblogs.microsoft.com/commandline/24-bit-color-in-the-windows-console/),
a [Pseudoconsole
("ConPTY")](https://devblogs.microsoft.com/commandline/windows-command-line-introducing-the-windows-pseudo-console-conpty/),
and more.

However, because Windows Console's primary goal is to maintain backward
compatibility, we have been unable to add many of the features the community
(and the team) have been wanting for the last several years including tabs,
unicode text, and emoji.

These limitations led us to create the new Windows Terminal.

> You can read more about the evolution of the command-line in general, and the
> Windows command-line specifically in [this accompanying series of blog
> posts](https://devblogs.microsoft.com/commandline/windows-command-line-backgrounder/)
> on the Command-Line team's blog.

### Shared Components

While overhauling Windows Console, we modernized its codebase considerably,
cleanly separating logical entities into modules and classes, introduced some
key extensibility points, replaced several old, home-grown collections and
containers with safer, more efficient [STL
containers](https://docs.microsoft.com/en-us/cpp/standard-library/stl-containers?view=vs-2022),
and made the code simpler and safer by using Microsoft's [Windows Implementation
Libraries - WIL](https://github.com/Microsoft/wil).

This overhaul resulted in several of Console's key components being available
for re-use in any terminal implementation on Windows. These components include a
new DirectWrite-based text layout and rendering engine, a text buffer capable of
storing both UTF-16 and UTF-8, a VT parser/emitter, and more.

### Creating the new Windows Terminal

When we started planning the new Windows Terminal application, we explored and
evaluated several approaches and technology stacks. We ultimately decided that
our goals would be best met by continuing our investment in our C++ codebase,
which would allow us to reuse several of the aforementioned modernized
components in both the existing Console and the new Terminal. Further, we
realized that this would allow us to build much of the Terminal's core itself as
a reusable UI control that others can incorporate into their own applications.

The result of this work is contained within this repo and delivered as the
Windows Terminal application you can download from the Microsoft Store, or
[directly from this repo's
releases](https://github.com/microsoft/terminal/releases).

---

## Resources

For more information about Windows Terminal, you may find some of these
resources useful and interesting:

* [Command-Line Blog](https://devblogs.microsoft.com/commandline)
* [Command-Line Backgrounder Blog
  Series](https://devblogs.microsoft.com/commandline/windows-command-line-backgrounder/)
* Windows Terminal Launch: [Terminal "Sizzle
  Video"](https://www.youtube.com/watch?v=8gw0rXPMMPE&list=PLEHMQNlPj-Jzh9DkNpqipDGCZZuOwrQwR&index=2&t=0s)
* Windows Terminal Launch: [Build 2019
  Session](https://www.youtube.com/watch?v=KMudkRcwjCw)
* Run As Radio: [Show 645 - Windows Terminal with Richard
  Turner](https://www.runasradio.com/Shows/Show/645)
* Azure DevOps Podcast: [Episode 54 - Kayla Cinnamon and Rich Turner on DevOps
  on the Windows
  Terminal](http://azuredevopspodcast.clear-measure.com/kayla-cinnamon-and-rich-turner-on-devops-on-the-windows-terminal-team-episode-54)
* Microsoft Ignite 2019 Session: [The Modern Windows Command Line: Windows
  Terminal -
  BRK3321](https://myignite.techcommunity.microsoft.com/sessions/81329?source=sessions)

---

## FAQ

### I built and ran the new Terminal, but it looks just like the old console

Cause: You're launching the incorrect solution in Visual Studio.

Solution: Make sure you're building & deploying the `CascadiaPackage` project in
Visual Studio.

> [!NOTE]
> `OpenConsole.exe` is just a locally-built `conhost.exe`, the classic
> Windows Console that hosts Windows' command-line infrastructure. OpenConsole
> is used by Windows Terminal to connect to and communicate with command-line
> applications (via
> [ConPty](https://devblogs.microsoft.com/commandline/windows-command-line-introducing-the-windows-pseudo-console-conpty/)).

---

## Documentation

All project documentation is located at [aka.ms/terminal-docs](https://aka.ms/terminal-docs). If you would like
to contribute to the documentation, please submit a pull request on the [Windows
Terminal Documentation repo](https://github.com/MicrosoftDocs/terminal).

---

## Contributing

We are excited to work alongside you, our amazing community, to build and
enhance Windows Terminal\!

***BEFORE you start work on a feature/fix***, please read & follow our
[Contributor's
Guide](./CONTRIBUTING.md) to
help avoid any wasted or duplicate effort.

## Communicating with the Team

The easiest way to communicate with the team is via GitHub issues.

Please file new issues, feature requests and suggestions, but **DO search for
similar open/closed preexisting issues before creating a new issue.**

If you would like to ask a question that you feel doesn't warrant an issue
(yet), please reach out to us via Twitter:

* Christopher Nguyen, Product Manager:
  [@nguyen_dows](https://twitter.com/nguyen_dows)
* Dustin Howett, Engineering Lead: [@dhowett](https://twitter.com/DHowett)
* Mike Griese, Senior Developer: [@zadjii@mastodon.social](https://mastodon.social/@zadjii)
* Carlos Zamora, Developer: [@cazamor_msft](https://twitter.com/cazamor_msft)
* Pankaj Bhojwani, Developer
* Leonard Hecker, Developer: [@LeonardHecker](https://twitter.com/LeonardHecker)

## Developer Guidance

## Prerequisites

You can configure your environment to build Terminal in one of two ways:

### Using WinGet configuration file

After cloning the repository, you can use a [WinGet configuration file](https://learn.microsoft.com/en-us/windows/package-manager/configuration/#use-a-winget-configuration-file-to-configure-your-machine)
to set up your environment. The [default configuration file](.config/configuration.winget) installs Visual Studio 2026 Community & rest of the required tools. There are two other variants of the configuration file available in the [.config](.config) directory for Enterprise & Professional editions of Visual Studio 2026. To run the default configuration file, you can either double-click the file from explorer or run the following command:

```powershell
winget configure .config\configuration.winget
```

### Manual configuration

* You must be running Windows 10 2004 (build >= 10.0.19041.0) or later to run
  Windows Terminal
* You must [enable Developer Mode in the Windows Settings
  app](https://docs.microsoft.com/en-us/windows/uwp/get-started/enable-your-device-for-development)
  to locally install and run Windows Terminal
* You must have [PowerShell 7 or later](https://github.com/PowerShell/PowerShell/releases/latest) installed
* You must have the [Windows 11 (10.0.26100) SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) installed at version 10.0.26100.8249 or greater.
* You must have at least [VS 2026](https://visualstudio.microsoft.com/downloads/) version 18.6 installed
* You must install the following Workloads via the VS Installer. Note: Opening
  the solution will [prompt you to install missing components automatically](https://devblogs.microsoft.com/setup/configure-visual-studio-across-your-organization-with-vsconfig/):
  * Desktop Development with C++
  * WinUI application development
* You must install the [.NET Framework 4.7.2 Targeting Pack](https://docs.microsoft.com/dotnet/framework/install/guide-for-developers#to-install-the-net-framework-developer-pack-or-targeting-pack) to build test projects

## Building the Code

OpenConsole.slnx may be built from within Visual Studio or from the command-line
using a set of convenience scripts & tools in the **/tools** directory:

### Building in PowerShell

```powershell
Import-Module .\tools\OpenConsole.psm1
Set-MsBuildDevEnvironment
Invoke-OpenConsoleBuild
```

### Building in Cmd

```shell
.\tools\razzle.cmd
bcz
```

## Running & Debugging

To debug the Windows Terminal in VS, right click on `CascadiaPackage` (in the
Solution Explorer) and go to properties. In the Debug menu, change "Application
process" and "Background task process" to "Native Only".

You should then be able to build & debug the Terminal project by hitting
<kbd>F5</kbd>. Make sure to select either the "x64" or the "x86" platform - the
Terminal doesn't build for "Any Cpu" (because the Terminal is a C++ application,
not a C# one).

> 👉 You will _not_ be able to launch the Terminal directly by running the
> WindowsTerminal.exe. For more details on why, see
> [#926](https://github.com/microsoft/terminal/issues/926),
> [#4043](https://github.com/microsoft/terminal/issues/4043)

### Coding Guidance

Please review these brief docs below about our coding practices.

> 👉 If you find something missing from these docs, feel free to contribute to
> any of our documentation files anywhere in the repository (or write some new
> ones!)

This is a work in progress as we learn what we'll need to provide people in
order to be effective contributors to our project.

* [Coding Style](./doc/STYLE.md)
* [Code Organization](./doc/ORGANIZATION.md)
* [Exceptions in our legacy codebase](./doc/EXCEPTIONS.md)
* [Helpful smart pointers and macros for interfacing with Windows in WIL](./doc/WIL.md)

---

## Code of Conduct

This project has adopted the [Microsoft Open Source Code of
Conduct][conduct-code]. For more information see the [Code of Conduct
FAQ][conduct-FAQ] or contact [opencode@microsoft.com][conduct-email] with any
additional questions or comments.

[conduct-code]: https://opensource.microsoft.com/codeofconduct/
[conduct-FAQ]: https://opensource.microsoft.com/codeofconduct/faq/
[conduct-email]: mailto:opencode@microsoft.com
[store-install-link]: https://aka.ms/terminal
