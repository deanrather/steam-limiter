# Introduction #

Making this required no reverse-engineering, it's just a simple matter of using standard Windows programming tools and techniques, starting with the Sysinternals [Process Explorer](http://technet.microsoft.com/en-us/sysinternals/bb896653).

# What does Steam Do #

Basically, when download Steam connects to a set of content servers from an internal list, and downloads from them all in parallel. This is good from a performance point of view, but problematic from the point of view of customers who would prefer that Steam use a single, specific content server provided by their ISP which is unmetered, such as [this one](http://www.telstraclear.co.nz/sub-sites/gaming-steam/).

If you double-click on an process in Process Explorer, it opens a sub-window with more additional data; the "TCP/IP" tab of this window shows the TCP/IP ports the application is using (a subset of the information included in the Windows [TcpView](http://technet.microsoft.com/en-us/sysinternals/bb897437) utility).

Using this, it's easy to see that Steam always maintains multiple connections open - using [TcpView](http://technet.microsoft.com/en-us/sysinternals/bb897437), it's even possible to try closing a connection underneath Steam, and it responds by immediately opening a new connection to a different content server. So, there's no way of stopping it using content servers which are ISP-metered (and thus, costly) and just using the ISP-provided free server as we would like.

# How to change it #

In the Process Explorer TCP/IP details, clicking on an open connection shows some interesting data, namely the content of the process stack at the time the connection was opened. This shows that the connection was made using the standard sockets [connect](http://msdn.microsoft.com/en-us/library/ms737625(VS.85).aspx) API rather than one of the other variant forms provided in Windows Sockets.

It's this observation, along with the lack of any existing utility which can prevent steam using unwanted content servers on Windows XP that lead to this connection limiter being written.

An under-appreciated facet of Windows NT is that it is, and always has been, designed for a good debugging experience. This includes having the facility [to patch and intercept system functions](http://blogs.msdn.com/b/oldnewthing/archive/2011/09/21/10214405.aspx) - normally just in your own process, but it's also a well-established technique (discussed in the MSDN journals and in documented code examples) to perform on arbitrary processes.

Therefore, all that's needed is to using the standard documented techniques to change the behaviour of Windows Sockets so that connections made to non-desirable content servers are prevented.

This doesn't require any reverse-engineering of Steam itself, its protocols, or any modification of any Steam code in memory; just to use the standard hooks put in place by Microsoft to modify the standard Windows Sockets function behaviour, albeit in a slightly unusual way.

# Some Caveats #

Putting hooks like this in place system-wide is easy; all kinds of quite normal applications (to say nothing of the more exotic things security software does) spray such things far and wide. Mostly, this is done by asking Windows to inject DLLs into all running processes, which can cause some awkwardness.

In particular, once you do this it's very hard to remove; the DLLs used tend to stay in use for a considerable time during which you can't touch them, so this is a common cause of the "need to reboot to upgrade" problem in software.

In addition, there are times we want to change whether Steam is being redirected; because there's an enormous amount of data to replicate to mirror content servers, an unmetered content server may not have the content for a particular game to download. Therefore, it's handy to be able to turn off the filtering if Steam gets stuck.

So, to make life easy it's best to have a technique which targets processes selectively, and which can easily remove itself from use - both to enable easy uninstallation and upgrade, and as well just to reconfigure or temporarily disable the action of a hook.

Finally, note that we don't want to simply reject Steam's calls to connect to undesirable servers by returning WSAECONNREFUSED - that seems to make it try harder. Instead, we let it connect - but rewrite the IP address to connect to so that it makes a duplicate connection to the only server we want. Steam seems to then recognise it's a duplicate later on, so that we end up with a nice steady state where Steam is quite happy and only one connection to the server we want is active.

# Shimming #

So, the limiter contains a monitor executable that looks for the Steam process (by looking for its' window) and then adds a dynamically-generated code shim to it, and starts a thread in the process.

That shim calls [LoadLibrary](http://msdn.microsoft.com/en-us/library/windows/desktop/ms684175(v=vs.85).aspx) and [GetProcAddress](http://msdn.microsoft.com/en-us/library/windows/desktop/ms683212(v=vs.85).aspx) using strings placed into the shim when it's built, and if those work it then calls the located function to activate or de-activate the filtering process, along with an optional parameter. Finally the shim then calls [FreeLibrary](http://msdn.microsoft.com/en-us/library/windows/desktop/ms683152(v=VS.85).aspx) and exits, so that the monitor process can deallocate the shim memory.

This process is versatile, but also very clean; the filter DLL adheres to the Windows rules and does not use forbidden APIs in the [DllMain](http://msdn.microsoft.com/en-us/library/windows/desktop/ms682583(v=VS.85).aspx) function, which is a major cause of subtle bugs and instability.

# How To Tell The Filter Is Working #

Mostly, the same tools above; [Process Explorer](http://technet.microsoft.com/en-us/sysinternals/bb896653) and [TcpView](http://technet.microsoft.com/en-us/sysinternals/bb897437) will display the particular servers that Steam is actually connecting to. Process Explorer in DLL view (hit Control-D) will show the components loaded in to the address space of a process so it's easy to see the filter DLL being loaded and unloaded from the Steam address space as well.

One additional Sysinternals utility of great value (especially to developers) is [DebugView](http://technet.microsoft.com/en-us/sysinternals/bb896647). This tool captures the output of the [OutputDebugString](http://msdn.microsoft.com/en-us/library/windows/desktop/aa363362(v=vs.85).aspx) API and logs it to a window - the filter DLL calls this on load and unload, and as well when it filters a call to the [connect](http://msdn.microsoft.com/en-us/library/ms737625(VS.85).aspx) Windows Sockets API.

Here is an example trace from [DebugView](http://technet.microsoft.com/en-us/sysinternals/bb896647) showing steam-limiter being loaded and then starting a Steam game download (in the text below, [9572](9572.md) is the Steam process ID ,which DbgView includes in log messages so you know where each message comes from).
```
[9572] wlgwpstmcon01.telstraclear.co.nz=203.167.129.4 
[9572] SteamFilter hook attached
[9572] CAPIJobRequestUserStats - Server response failed 2
[9572] Connect redirected 
[9572] Connect redirected 
[9572] Connect redirected 
[9572] Connect redirected 
[9572] ExecCommandLine: ""C:\Program Files\Steam\steam.exe" steam://open/downloads "
[9572] ExecSteamURL: "steam://open/downloads"
[9572] CAPIJobRequestUserStats - Server response failed 2
```

The exact debug text produced by steam-limiter changes in each version, but the above shows the steam-limiter hook DLL processing the [filter rule](FilterRules.md) inside the Steam process, and resolving the DNS name of the TelstraClear Steam server to a numeric IP which it will use to redirect Steam; the "Connect redirected" messages are Steam trying to connect on port 27030 to a list of content servers, which the hook DLL then redirects to the IP we want to connect it to.