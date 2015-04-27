# Host Filtering #

With the introduction of tighter filtering in steam-limiter 0.6.1 it turned out that some ISP-level CDNs were not following the normal behaviour of any other pre-existing Steam CDNs.

Specifically, steam.cdn.on.net was the first Steam server that I was made aware of that did **not** respond at all when requests were made to it under a name of the form `content?.steampowered.com`. Additionally, this server was configured to also not return content to anyone in the world who was not a customer of the specific ISPs allowed to access it.

This meant that to gain access to this download server, Valve were advertising it specifically under the name `steam.cdn.on.net` , and they were only advertising that server it to the customers of the ISP who operate it because it is inaccessible to everyone else.

This means that steam-limiter has to be cleverer about how it filters things, and that the new (and very leakproof) filtering in steam-limiter 0.6 had to be retired in order to deal with this style of server configuration.

## How steam-limiter copes ##

In versions later than steam-limiter 0.6.1 I have introduced yet another filtering system that can distinguish outgoing HTTP requests made by Steam not only on the basis of the specific URL being requested, but also on the basis of the specific name of the machine being passed with that request in [the HTTP Host: header](http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.23).

New rules can begin with two '/' characters to identify the host part of the outgoing HTTP request and whether to pass or reject the outbound request.

Internally, new steam-limiter versions have a built-in rule which passes all requests to download content from `content*.steampowered.com` and which rejects all other hosts; this is so steam-limiter can prevent other servers that Valve advertised which aren't necessarily unmetered.

To whitelist and this permit a server like `steam.cdn.on.net` which uses a nonstandard name, a rule element to permit it can be included:
```
//steam.cdn.on.net=*
```

## New rule UI ##

Because older versions of steam-limiter don't understand this new filter rule syntax, the webservice that automatically selects Steam rules now sends back two suggested rules.

One of these is the old rule format, for use with older versions of steam-limiter.

The other filter rule is a list of host-specific filters like the above example if your Steam server is known to require it, as the Internode/iiNet servers in Australia do.

In reality behind the scenes, the new versions of steam-limiter then just join the two rule pieces together into a single rule. This not only makes it possible to continue supporting old versions of steam-limiter but also an expanded rule editor that keeps the two rule pieces separate (as some custom rules people were making are getting rather complicated).