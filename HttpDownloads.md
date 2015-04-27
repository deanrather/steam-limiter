# Steam's use of HTTP for downloading #

The classic Steam content delivery network (or CDN, a commonly-used term in global-scale Web deployment) delivers products via a custom protocol which is hosted on TCP port 27030.

For their own reasons, Valve have added a parallel CDN which instead of using a custom protocol on port 27030, instead uses the plain HTTP 1.1 protocol. Since the Steam client also retrieves the store content over HTTP (using an embedded copy of the WebKit browser to display it) this means using a slightly different technique to filter steam's HTTP downloads than to filter the classic downloads.

## Use of HTTP is per-product ##

Whether the classic CDN or the new HTTP CDN is used appears to be a per-product setting; when you start downloading a game via Steam, one of two entirely different sequences of actions occurs depends on whether the product is marked for the new or old download systems.

## Indirection over HTTP ##

When the Steam client uses the new download system, it first requests some download information from Steam over HTTP; this returns a text document laid out roughly like [the JSON format](http://json.org/) (although, strangely, it's not exactly in JSON format, which is quite peculiar given how widely-used JSON is for data in web systems).

This payload contains server IPs for the next level of indirection, returning a manifest which directs the Steam client what to do next.

This is processed (and the servers listed in Steam's response are contacted) during the "Preparing files for installation" phase, before actual download begins, but then it smoothly transitions into the main download over HTTP.

During the "preparing files for installation" phase the Steam clients loads a document of type application/x-steam-manifest (several copies, from various of the suggested servers) before entering the regular download phase, and during the regular download phase it requests documents of type application/x-steam-chunk from exactly the same set of servers.


## Server resolution ##

In the classic Steam download protocol, the list of servers to use is retrieved over port 27033 and contains a set of binary data with a list of server IPs and port numbers for use with the classic protocol.

In the new HTTP download, the server list is in text format; this means that in most cases the Steam client will go through an extra step to resolve the server's name into an IP address before contacting it.

In some web-scale application, this process of converting a name into an IP address [can return a different value to perform load balancing](http://en.wikipedia.org/wiki/Round-robin_DNS), and so the fact Steam appears to be returning a small set of content server names doesn't necessarily form a guide as to how large a pool of servers there are or where they are geographically located, and this can make filtering them through firewalling techniques complex.

## Using Name resolution for filtering ##

As long as Steam consistently uses server names, the filtering done by the Steam Limiter can help with this; depending on the server name, particular servers can be blacklisted or whitelisted by name.

In addition, it's possible to blacklist, whitelist, or replace the set of returned results from the DNS query performed by the Steam client, just as the Steam Limiter does for connection attempts.

In Steam Limiter v0.4.0, only simple blacklisting is being done based on the DNS name as a way of doing simple blocking - this is a temporary measure since a number of major new game releases on Steam (most notably, Skyrim) are using HTTP downloading, and to help customers manage their downloads it seems best to block all HTTP access by default.

Later versions will refine this as it becomes more apparent what strategy Valve are using with referring to ISP-specific servers using HTTP. At present there are no ISP-specific servers supporting HTTP that I can test against to know what filtering strategy will work best; once more information on this is available the filtering strategy can be refined in later versions.

## URL-level filtering ##

It turns out that when making an HTTP download request, the Steam client code submits the HTTP request to be sent over the network as a single call to the `WSASend()` API. By intercepting this API call and parsing out the HTTP verb and URL, it's therefore simple to consult the filter system with the URL string to determine whether the request should be allowed or denied.

In the first instance, this is being employed not to block normal HTTP downloads (which are managed by DNS redirection), which is used by the main Steam HTTP CDN. However, there is an additional separate HTTP download system using what Steam calls "CS" servers - these servers are identified by numeric IP and so bypass the DNS redirection done for normal HTTP downloads, and in the few cases where they appear to be used they tend to refer to servers which are not in the same region; in particular, users in South Africa are being referred to HTTP download servers in Europe.

As such, from v0.5.2.0 onwards there is a built-in rule in the steam-limiter application which blocks the use of the "CS" type filters by matching a URL of "/initsession/" and returning an error to Steam indicating that the remote peer has dropped the connection.

This URL is actually used to log into the "CS" type server by sending your current Steam login cookie to it; as such, it's a part of the HTTP download process unique to the "CS" server type and so blocks them completely, since they appear to basically never point to a server that will be unmetered.