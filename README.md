<p align="center">
 <a href="https://github.com/radj307/volume-control-cli"><img src="https://i.imgur.com/lTPPocx.png"></a><br/>
 <a href="https://github.com/radj307/volume-control-cli/releases/latest"><img alt="Latest Tag Shield" src="https://img.shields.io/github/v/tag/radj307/volume-control-cli?label=Version&logo=github&style=flat-square"></a>&nbsp;&nbsp;&nbsp;<a href="https://github.com/radj307/volume-control-cli/releases"><img alt="Total Downloads Shield" src="https://img.shields.io/github/downloads/radj307/volume-control-cli/total?label=Downloads&logo=github&style=flat-square"></a>
</p>

***

Commandline app-specific volume control utility for Windows.  

This is the commandline version of **[Volume Control](https://github.com/radj307/volume-control)**, which adds fully configurable & extensible hotkeys for controlling the mixer volume level of specific apps *(and much more)*.  


## Features

- Supports both Audio Input & Audio Output Devices
- Supports changing the volume/mute state of specific processes
- View the ProcessId & ProcessName of all active audio sessions
- Interactive device/session queries (`-Q`|`--query`)
- Custom flexible GNU-style syntax
- Supports minimal I/O for shell scripts (`-q`|`--quiet`)
- Built-in documentation (`vccli -h`)


## Installation

 1. Download the [latest release](https://github.com/radj307/vccli).
 2. Extract to a directory of your choice.

> ### :warning: Important
> If you want to be able to call `vccli` from any working directory without specifying the full path, you'll have to add the directory where you placed `vccli.exe` to [your PATH environment variable](https://stackoverflow.com/a/44272417/8705305).  
> Once you've done that, restart your terminal emulator to refresh its environment & you'll be able to call `vccli` from anywhere.  

## Usage

Run `vccli -h` in a terminal to see the built-in usage guide & the most up-to-date documentation.  
