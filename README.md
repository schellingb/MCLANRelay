Minecraft LAN Relay
===================

![Minecraft LAN Relay on Windows](https://raw.githubusercontent.com/schellingb/MCLANRelay/master/README.png)

# Overview
This is a tool for both client and server side to relay/proxy a Minecraft LAN game over the internet.

## Download
You can find the Windows build on the [Releases page](https://github.com/schellingb/MCLANRelay/releases/latest).  
For Linux and macOS you need to run `build-clang.sh` or `build-gcc.sh` to build it.

## Usage
On Windows there are two similar executables `MCLANRelay.exe` and `MCLANRelay-Cmd.exe`.  
The first one runs as an icon in the systray (on the bottom right, next to the clock) like in the screenshot above.  
The second one runs in the Windows command-line (Cmd.exe) like the Linux and macOS versions.

To start up the tool, you need to set the desired mode via program arguments.  

On Windows it's easiest to right-click the executable (after extracting it) and selecting 'Create shortcut'
followed by right-clicking the new shortcut and selecting 'Properties'. Then at the end of the 'Target' field you
can add the desired program arguments.

### Server-Side Mode
On the server side, the tool listens for LAN server announcements.

The first announced server is then forwarded onto the computer that runs this tool on port 25565.
If there are more than one server running on the LAN the second one will be forwarded to port 25566 and so on.

To let players join over the internet, the port needs to be forwarded on the router to the computer running MCLANRelay.

Program argument: `-s [<port>]`

 Argument   | Explanation
------------|-------------
 `-s`       | Operate in server-side mode
 `[<port>]` | Optional: Customize the first port used for forwarding (defaults to 25565)

### Client-Side Mode
On the client side, this tool advertises a fake LAN server that actually connects to an online one.

The actual online server needs to be set through the program arguments.
The online server can be a regular game server or a server running through MCLANRelay in server-side mode.

With other optional parameters the name shown in the server browser can be customized as 
well as the local port used by the relay. The local port should only need to be customized
when running multiple instances of MCLANRelay in client-side mode.

Program argument: `-c <online-server-host> [<server-port>] [<local-port>] [<name>]`

 Argument               | Explanation
------------------------|-------------
 `-c`                   | Operate in client-side mode
 `<online-server-host>` | Required: IP address or host-name of the online server to connect to
 `[<server-port>]`      | Optional: Port number of the online server (defaults to 25565)
 `[<local-port>]`       | Optional: Local port used by the relay (default 51235)
 `[<name>]`             | Optional: The name that shows up in the server browser (default 'MCLANRelay')<br>Can be specified even when no port numbers have been passed (as it is non-numerical)

### Other options
There are generic options applicable for all modes.

 Argument          | Explanation
-------------------|-------------
 `-b <ip-address>` | Specify the network interface to listen on (defaults to all)
 `-h`              | Show the program argument reference then exit

# License
MCLANRelay is available under the [Unlicense](http://unlicense.org/) (public domain).
