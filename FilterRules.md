# Expanded Rule Language #

In the first versions of the Steam Limiter, only a single IPv4 address could be specified for restricting Steam downloads. In actual fact a DNS name could be used, but it was easier to use raw IP addresses, and since New Zealand ISPs generally only supported a single filtered content server this was adequate.

As of v0.5 of Steam Limiter, this is being changed to make the rules more general; the rules for changing what server Steam to connect to can list multiple IP addresses or DNS names, and can be combined with a pattern for controlling how Steam resolves DNS names for the newer HTTP content server system which Steam are still deploying.

# Concepts #

The implementation in v0.4 for HTTP download filtering used a hard-coded rule which matched a DNS name to block results for using a [wildcard pattern](http://en.wikipedia.org/wiki/Glob_(programming)) called a "glob" based on the idea in the UNIX shell interpreter - the same basic patterns also being in [CP/M and MS-DOS](http://en.wikipedia.org/wiki/CP/M#Legacy) and thus were inherited as a convention in Windows.

In v0.5 this idea was expanded a little to allow multiple patterns to be specified and allow both DNS name patterns (which match general strings) and also the same kind of wildcards to be used to match IP addresses in the Windows Sockets connect API by rendering the target address as a string and then matching it the same way.

This, a simple filter rule which was previously hard-coded to match port 27030 could be represented the same way as a DNS filter rule by a pattern like this:<dl><dd><code>*:27030=203.167.129.4</code></dd></dl>

Given this, adding some additional syntax for supporting rules to be joined together and for multiple IP addresses to appear allows a potentially quite complex rule set for the Steam Limiter to be specified as a string.

In addition, by making the port numbers explicit rather than hardcoding them, the techniques used by the limiter's filter DLL can usefully be applied to other programs. For example, a common problem in commercial software production is the use of [Build acceptance testing](http://en.wikipedia.org/wiki/Acceptance_testing) as part of a [continuous integration process](http://en.wikipedia.org/wiki/Continuous_integration) such as that provided by the [Jenkins build tool](http://jenkins-ci.org/).

Writing acceptance tests for complex commercial software often involves exercising network functions, but because network functions in code are often very complex, for acceptance tests to be meaningful they can't be performed with the network code replaced by a test stub. The techniques demonstrated in the Steam Limiter are directly applicable in acceptance testing of networked applications in Windows, using API hooking to exert fine control over simulated network behaviour, including such things as fault injection, and the expanded rule scripts for v0.5 are intended to demonstrate a roadmap for building such a test tool on this foundation.

# Rule Syntax #

The overall rule for the Steam Limiter is that a rule set contains a sequence of rules separated by semicolons.

Individual rules consist of two parts: a pattern and a replacement, separated by an equals sign. The replacement part consists of a list of replacement IP host names (in either DNS-name or numeric IP form), separated by commas, optionally containing a port number.

Patterns come in two kinds; those without a port number, which apply to DNS operations, and those with a port number, which apply to connect operations.

The formal definition of the syntax for rules therefore looks something like this:

```
rules   ::== <rule> (';' <rule>)*
rule    ::== <replace> (',' <replace>)*`
rule    ::== <pattern> '=' [<replace> (',' <replace>)*]
pattern ::== <glob-pattern> [':' <port>]
replace ::== <host> [':' <port>] ['#' <comment>]
```

A subtle feature of the above is that as a way of allowing plain server IP addresses from v0.4.1 and earlier versions of the Steam Limiter to be used as rules, any rule that does not have an equal sign in it is considered as implicitly being a "legacy rule", and is treated as if the pattern `*:27030=` is appended in front of it to make it a legal rule in the new system.

# Rule Processing #

Rules are matched from left to right in order, and the first rule which matches the pattern is applied. If any replacement IP addresses (and optionally, port numbers) are supplied then the replacements are used in round-robin fashion to override the behaviour of the Windows Sockets APIs.

If the pattern part of a rule matches and the replacement section of the rule is empty, then that is considered a "blocking" rule and the API call is failed rather than being allowed to complete.

In v0.5 of Steam Limiter the existing HTTP blocking rule in the v0.4 versions is always attached to the end of the specified rules by default. In the new syntax this rule looks like this: <dl><dd><code>content?.steampowered.com=</code></dd></dl>

Because rules are always processed left to right, this default rule can be effectively overridden by putting other rules in place which take effect instead.

# Future Extensions #

If necessary, future releases will allow the Steam Limiter to trigger other actions such as launching executables or writing log data as part of the replacement rule syntax.

The primary consideration here is expected to be the need to interpose a local proxy application for handling HTTP interchanges made by steam to the CDN, in case it proves useful to be able to replace the download metadata provided by Valve's HTTP servers at the start of the download process. Since such a proxy would only be needed on rare occasions, it should be possible to launch it on demand and so some rule syntax will be needed to specify the application to launch as well as how to rewrite the connection.