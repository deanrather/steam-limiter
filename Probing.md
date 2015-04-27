# Introduction #

Added to steam-limiter version 0.5.4.0 is a system for assisting with automatic ISP detection, designed for users in South Africa where it's common to have more than one upstream ISP.

This describes the detailed mechanics of the expanded detection system, which can be used in future to bring more improvements to the way steam-limiter rules work.

# Dual ISPs #

Because South Africa has not undergone the local-loop unbundling regulatory transition that many other countries have, the pricing structures in the ISP market there are unusual for an advanced economy. As such, it's common for home users to have an "unlimited-data" account with one ISP as well as a data-metered account with a second ISP such as [WebAfrica](http://www.webafrica.co.za) who provide a range of unmetered customer services they host locally.

This situation complicates things for the system I had designed for Steam Limiter to autoconfigure, because in general the configuration request to the [web service](AppEngine.md) where I maintain the Steam Limiter rules will only provide an indication of one of the available ISPs.

Hence, from version 0.5.4.0 I have added an [additional component](http://code.google.com/p/steam-limiter/source/browse/#hg%2Fprobe) to the Steam Limiter install to help with this. If the [web service](AppEngine.md) detects an install from a South African ISP, in addition to the normal rule suggestion it will send back a set of additional tests that can be performed to detect a second ISP.

When the install script runs these tests, it also sends back an indication of whether the test passed or failed to the App Engine webservice, since otherwise it's impossible to diagnose how effective the test system is.

# Using the probe executable manually #

The first attempt at creating the probe executable tested whether a network service was available at a hostname; this didn't turn out to be as useful as first hoped, but it has other potential uses and so it's still available in the executable. This can be invoked manually like this:

```
C:\Program Files\LimitSteam>probe steam.wa.co.za 80 show
```

The above example tests whether HTTP connectivity is available from the Steam content server run by [webafrica](http://www.webafrica.co.za/), and returns a result of 0 if it is accessible or 1 if it isn't.

The second system designed for helping detect the ISPs is based on a similar technique to the [traceroute](http://en.wikipedia.org/wiki/Traceroute) utility found in most UNIX systems and in Windows under the name "tracert".

When used this way, the probe utility is given a DNS name pattern similar to the kind of DNS pattern used in the [filter rules](FilterRules.md). As it traces the connection to a named system, it requests the official names of each address it finds - since home DSL routers generally don't have names, the first system with a name that a traceroute finds will generally belong to the ISP.

This command runs a simple trace and prints the names of the machines it finds:
```
C:\Program Files\LimitSteam>probe steam.wa.co.za icmp
```

Running a trace like this helps Steam Limiter detect complex routing configurations. If one ISP connection is used for Steam, but another for Web browsing, because the above trace targets a Steam content server it will follow the same traffic path that Steam will use.

This example shows what the ISP test actually used by Steam limiter tries to do:
```
C:\Program Files\LimitSteam>probe steam.wa.co.za icmp *.wa.co.za show
```
In this case, the first system with a name the traceroute finds is matched against `*.wa.co.za` to determine if a direct connection to WebAfrica's unmetered steam server exists, and so what filter rule to use.

Finally, there is also a system for comparing ping times between two hosts (or between a host and a fixed ping value); this collects up to 4 ping times from each hosts and compares the average and returns 0 or 1 depending on whether the left-hand or right-hand host has a lower value:
```
C:\Program Files\LimitSteam>probe steam.wa.co.za vs steam2.wa.co.za show
```

This is intended to help tune rule selections by making it possible to detect differences in performance due to routing issues or simple geography between customers in different locations such as Perth or Sydney where customers on the same ISP might benefit from slightly different selection rules.

# Other probe uses #

Tests like the above may be used in future with non-African ISPs to help test and maintain the Steam Limiter rules, because currently most ISPs do not allow their content servers to be used for HTTP downloads using the new Steam content delivery system.

Over the next while, some ISPs will no doubt be changing their server configuration to add this capability, but it's unlikely they will announce it. By occasionally adding the above tests into the rules for Steam Limiter it will be possible to discover this and improve the filter rules to take advantage of this.