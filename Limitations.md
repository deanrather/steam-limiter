# Limitations of Filtering #

As described in the [theory of operation](HowItWorks.md) page, Steam's new patching system will sometimes use HTTP to download content; as the Steam client has plenty of good reasons to be using HTTP for the store, any filtering that is done for content download needs to be done carefully to avoid interfering with the operation of the built-in WebKit browser that Steam uses to display the store.

In addition to the long-awaited new delta-patching and HTTP download systems in Steam (which are not, strictly speaking, technically related but in practice they appear to be operationally connected), certain content appears to be wired to always download via HTTP regardless, and like the above this will still evade filter restrictions for Steam Limiter versions prior to v0.4.0, while for v.0.4.0 and later [a simple system for restricting http](HttpDownloads.md) has been put in place.

The primary game I use for testing HTTP downloads is Evil Genius, since it is a smaller, inexpensive and older game.

# Steam holds connections Open #

After pausing a download which is in progress, Steam holds the connection(s) it was using to download data from open for a short time (a minute or so) in order to drain them of data in flight and so that pausing and resuming a download quickly doesn't require it to start over again.

Since the current versions of the download limiter don't attempt to close any currently-open connections to different download servers, rapidly trying to change the filtered server may not work if:
  1. A download is in progress, or is paused.
  1. The limiter is first started or is pointed at a new server.
  1. The download is then resumed within Steam without waiting long enough for Steam to disconnect from its previous content server.

So if Steam has been downloading, and you pause it to change to load the limiter, you **must** either wait a minute or so, or use [TcpView](http://technet.microsoft.com/en-us/sysinternals/bb897437) to ensure that Steam has actually disconnected from the content server before resuming the download.

Under normal circumstances, the limiter will always be running when Steam is, and Steam will never get a chance to see a wrong content server. The above just applies when adjusting the limiter's configuration - if you're rapidly stopping and resuming downloads (as when you're testing the limiter to see that it works) sometimes you can inadvertently do that.

# Steam caches DNS results #

The HTTP blocking feature of Steam Limiter relies on modifying the results of DNS queries performed by the Steam client. When it first attempts to perform a download using Valve's new HTTP CDN, the Steam client requests a document via HTTP from a master server that describes where the main file content can be found.

This document identifies servers to use with DNS names rather than binary IP addresses, and so the Steam client then resolves these names. For now, the names tend to be of the form `content<number>.steampowered.com` and so the Steam Limiter hooks the DNS query API `gethostbyname ()` and returns a failure instead of letting Steam use Valve's master content servers.

However, because the Steam client remembers the results of these DNS queries itself, once Steam has performed an HTTP download it won't ask again. This means that once Steam has done an HTTP download, Steam Limiter won't block it.

Since the way Steam Limiter is designed is to be left running continuously anyway (as Steam can and will decide to download large things like game updates on its own), as with the above this is mainly a concern when you enable or disable it. As long as Steam Limiter is running before the Steam client is, or you restart Steam if you disable the Steam Limiter for a time (such as to download a game from metered servers if you can't wait for it to be loaded onto an unmetered one) then this won't be a problem.

Once the technique described below for the CS download server type makes it into the main release of steam-limiter (which should be in version 0.5.2) then once that receives wider testing that alternate filter approach can replace DNS blocking as the default, which will allow HTTP filtering to be enabled/disabled quickly without these caching effects.

# CS versus CDN HTTP Servers #

When the Steam client begins an HTTP download, it begins by requesting a list of servers to use from an initial seed webservice; since the GeoIP selection done via DNS is quite crude and inaccurate (to put it mildly) this webservice provides an alternative way that Valve can make the HTTP download system flexible.

The exact document that is sent back varies quite a bit depending on the ISP the request is made from; the "CDN" download entries using content?.steampowered.com are essentially fixed, but this example shows Valve suggesting a number of additional content servers using numeric IPs, which means they evade the CDN redirection that steam-limiter does:

```
"serverlist"
{
	"0"
	{
		"type"		"CDN"
		"vhost"		"content1.steampowered.com"
		"host"		"content1.steampowered.com"
		"load"		"0"
		"weightedload"		"100.000000"
		"sourceid"		"1"
	}
	"1"
	{
		"type"		"CDN"
		"vhost"		"content3.steampowered.com"
		"host"		"content3.steampowered.com"
		"load"		"0"
		"weightedload"		"100.000000"
		"sourceid"		"4"
	}
	"2"
	{
		"type"		"CS"
		"sourceid"		"41"
		"cell"		"4"
		"load"		"34"
		"weightedload"		"194.000000"
		"host"		"209.197.10.83:80"
		"vhost"		"209.197.10.83:80"
	}
	"3"
	{
		"type"		"CS"
		"sourceid"		"42"
		"cell"		"4"
		"load"		"40"
		"weightedload"		"200.000000"
		"host"		"209.197.10.82:80"
		"vhost"		"209.197.10.82:80"
	}
	"4"
	{
		"type"		"CS"
		"sourceid"		"45"
		"cell"		"4"
		"load"		"40"
		"weightedload"		"200.000000"
		"host"		"212.187.201.122:80"
		"vhost"		"212.187.201.122:80"
	}
}
```

I had hoped that the Steam client was using a standard Windows Sockets API for parsing the "host" strings, but it turns out that it doesn't, so there is no direct way of filtering these, and steam-limiter as of v0.5.1 cannot block these (although fortunately since the "CS" server suggestions are so wildly inappropriate, the download performance from them is tragic and data leakage should be small).

However, what the "CS" type connection really indicates is that the content server is authenticating; the Steam client always issues an identifying POST request to the server as the first item on connection, and so by identifying when the `/initsession/` URL is requested, as along as steam-limiter is running when the download is initiated it should be possible to block these in versions of steam-limiter after v0.5.1